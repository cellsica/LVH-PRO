#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_dsp/juce_dsp.h>
#include <functional>
#include <atomic>
#include <array>
#include <map>
#include "SineWaveProcessor.h"
#include "PluginSlot.h"
#include "MidiInjectionsProcessor.h"
#include "../BridgeProcessors.h"
#include "../MetronomeManager.h"
#include "ProcessorPluginNode.h"

using namespace juce;

/**
 * @class GainAndMeterProcessor
 * @brief Audio graph node that applies master output gain and measures levels.
 *
 * Inserted as the last processing node before the AudioOutput node in every
 * graph configuration.  Performs three tasks on the audio thread:
 *
 * 1. **Gain** — applies a linear master output gain (atomic, set from the message thread).
 * 2. **Peak / RMS metering** — captures per-channel peak and IIR-smoothed RMS,
 *    published via atomics for the VU meter timer on the message thread.
 * 3. **FFT + waveform capture** — accumulates the mono-summed signal into a
 *    1024-sample ring buffer, applies a Hann window, computes a 512-bin FFT,
 *    and publishes the magnitudes under a SpinLock for the VisualizerManager.
 *
 * **Thread safety:**
 * - processBlock() runs on the audio thread and writes atomics / SpinLock-protected buffers.
 * - exchangePeak(), exchangeRms(), readFFTMagnitudes(), readWaveform() are called
 *   from the message thread.  readFFTMagnitudes() / readWaveform() use a try-lock and return 0
 *   without blocking if the audio thread is mid-write.
 * - setGain() is atomic and safe to call from any thread.
 */
class GainAndMeterProcessor : public AudioProcessor
{
public:
    GainAndMeterProcessor()
        : AudioProcessor (BusesProperties()
              .withInput  ("Input",  AudioChannelSet::stereo(), true)
              .withOutput ("Output", AudioChannelSet::stereo(), true))
    {
        gain.store    (1.f);
        peaks[0].store (0.f);
        peaks[1].store (0.f);
        rms[0].store (0.f);
        rms[1].store (0.f);
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        peaks[0].store (0.f);
        peaks[1].store (0.f);
        rms[0].store (0.f);
        rms[1].store (0.f);
        rmsSmoothed[0] = 0.f;
        rmsSmoothed[1] = 0.f;

        // IIR smoothing coefficient — 50ms time constant.
        // Physics engine handles VU ballistics, so we just need a stable short-window RMS.
        const double tau = 0.05;
        rmsAlpha = (float)(1.0 - std::exp (-(double)samplesPerBlock / (tau * sampleRate)));
    }
    void releaseResources() override {}

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        float g = gain.load (std::memory_order_relaxed);
        const int numSamples = buffer.getNumSamples();
        for (int ch = 0; ch < jmin (2, buffer.getNumChannels()); ++ch)
        {
            buffer.applyGain (ch, 0, numSamples, g);

            // Peak
            float p = buffer.getMagnitude (ch, 0, numSamples);
            float cur = peaks[ch].load (std::memory_order_relaxed);
            if (p > cur) peaks[ch].store (p, std::memory_order_relaxed);

            // RMS — block-level then IIR-smoothed
            const float* data = buffer.getReadPointer (ch);
            float sumSq = 0.f;
            for (int i = 0; i < numSamples; ++i)
                sumSq += data[i] * data[i];
            float blockRms = std::sqrt (sumSq / (float)numSamples);
            rmsSmoothed[ch] += rmsAlpha * (blockRms - rmsSmoothed[ch]);
            rms[ch].store (rmsSmoothed[ch], std::memory_order_relaxed);
        }

        // ── FFT + Waveform accumulation ─────────────────────────────────
        // Mix L+R to mono and accumulate into fftAccum_.
        const float* L = buffer.getReadPointer (0);
        const float* R = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : L;
        for (int i = 0; i < numSamples; ++i)
        {
            fftAccum_[fftPos_] = (L[i] + R[i]) * 0.5f;
            ++fftPos_;
            if (fftPos_ >= fftSize_)
            {
                // Apply Hann window and run FFT (in-place, frequency-only).
                std::copy_n (fftAccum_.begin(), fftSize_, fftWork_.begin());
                window_.multiplyWithWindowingTable (fftWork_.data(), fftSize_);
                // Zero imaginary part before FFT
                std::fill (fftWork_.begin() + fftSize_, fftWork_.end(), 0.f);
                fft_.performFrequencyOnlyForwardTransform (fftWork_.data());

                // Publish under lock (SpinLock — fast, audio-safe)
                {
                    juce::SpinLock::ScopedLockType lock (fftPubLock_);
                    const float norm = 1.0f / (float)fftSize_;
                    for (int k = 0; k < fftBins_; ++k)
                        fftMag_[k] = fftWork_[k] * norm;
                    std::copy_n (fftAccum_.begin(), fftSize_, waveformPub_.begin());
                }
                fftPos_ = 0;
            }
        }
    }

    /**
     * @brief Read the latest FFT magnitude spectrum into @p buf.
     *
     * Uses a try-lock: if the audio thread is mid-write, returns 0 immediately
     * without blocking.  Called from the message thread (VisualizerManager).
     *
     * @param buf   Caller-allocated output buffer.
     * @param size  Maximum number of bins to copy.
     * @return      Number of bins written, or 0 if the lock could not be acquired.
     */
    int readFFTMagnitudes (float* buf, int size) const
    {
        juce::SpinLock::ScopedTryLockType tryLock (fftPubLock_);
        if (! tryLock.isLocked()) return 0;
        const int n = juce::jmin (size, fftBins_);
        std::copy_n (fftMag_.begin(), n, buf);
        return n;
    }

    /**
     * @brief Read the latest mono waveform block into @p buf.
     *
     * Uses a try-lock — returns 0 without blocking if the audio thread is writing.
     * Called from the message thread (VisualizerManager).
     *
     * @param buf   Caller-allocated output buffer.
     * @param size  Maximum number of samples to copy.
     * @return      Number of samples written, or 0 if the lock could not be acquired.
     */
    int readWaveform (float* buf, int size) const
    {
        juce::SpinLock::ScopedTryLockType tryLock (fftPubLock_);
        if (! tryLock.isLocked()) return 0;
        const int n = juce::jmin (size, fftSize_);
        std::copy_n (waveformPub_.begin(), n, buf);
        return n;
    }

    static constexpr int fftOrder_ = 10;
    static constexpr int fftSize_  = 1 << fftOrder_;  ///< FFT window size: 1024 samples.
    static constexpr int fftBins_  = fftSize_ / 2;    ///< Number of magnitude bins: 512.

    /**
     * @brief Atomically read and reset the output peak for channel @p ch.
     *
     * Call from the message thread only (e.g. a repaint timer).
     * @param ch  0 = Left, 1 = Right.
     * @return    Peak magnitude since the last call.
     */
    float exchangePeak (int ch)
    {
        if (ch < 0 || ch > 1) return 0.f;
        return peaks[ch].exchange (0.f, std::memory_order_relaxed);
    }

    /**
     * @brief Read the latest IIR-smoothed RMS for channel @p ch.
     *
     * Uses load (not exchange) so the VU physics engine always sees the
     * current value regardless of timer / audio-block timing.
     * Call from the message thread only.
     *
     * @param ch  0 = Left, 1 = Right.
     * @return    Smoothed RMS value.
     */
    float exchangeRms (int ch)
    {
        if (ch < 0 || ch > 1) return 0.f;
        return rms[ch].load (std::memory_order_relaxed);
    }

    /**
     * @brief Set the master output gain.
     *
     * Atomic — safe to call from any thread.
     * @param g  Linear gain value (1.0 = unity, 0.0 = silence).
     */
    void setGain (float g) noexcept { gain.store (g, std::memory_order_relaxed); }

    // AudioProcessor boilerplate
    const String getName() const override               { return "LVH Gain+Meter"; }
    double getTailLengthSeconds() const override        { return 0.0; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool hasEditor() const override                     { return false; }
    AudioProcessorEditor* createEditor() override       { return nullptr; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const String getProgramName (int) override          { return {}; }
    void changeProgramName (int, const String&) override{}
    void getStateInformation (MemoryBlock&) override    {}
    void setStateInformation (const void*, int) override{}

private:
    std::atomic<float> gain;
    std::atomic<float> peaks[2];

    std::atomic<float> rms[2];
    float rmsSmoothed[2] = {};  ///< IIR state — audio thread only.
    float rmsAlpha       = 0.1f;

    // ── FFT pipeline ──────────────────────────────────────────────────────────
    // Audio thread writes; message thread reads via try-lock SpinLock.
    juce::dsp::FFT                      fft_    { fftOrder_ };
    juce::dsp::WindowingFunction<float> window_ { (size_t)fftSize_,
                                                   juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize_>         fftAccum_ {};       ///< Accumulation ring — audio thread only.
    std::array<float, fftSize_ * 2>     fftWork_  {};       ///< Interleaved FFT work buffer.
    int                                 fftPos_   = 0;      ///< Write position in fftAccum_.
    mutable juce::SpinLock             fftPubLock_;         ///< Protects fftMag_ and waveformPub_.
    std::array<float, fftBins_>         fftMag_      {};    ///< Published FFT magnitudes.
    std::array<float, fftSize_>         waveformPub_ {};    ///< Published waveform snapshot.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainAndMeterProcessor)
};

// =============================================================================

/**
 * @class InputGainProcessor
 * @brief Audio graph node for physical audio input gain, mute, and mono-fold.
 *
 * Inserted between the audioInputNode and the first master FX node (or the
 * GainAndMeterProcessor when no master FX are loaded).  Provides:
 *
 * - **Input gain** — linear scalar applied to both channels (atomic).
 * - **Mute** — zeroes both channels when enabled (atomic).
 * - **Mono fold** — sums L+R to mono and copies to both outputs (atomic).
 * - **Input peak metering** — per-channel peak captured for the input meter.
 *
 * **Thread safety:** All setters are atomic and safe to call from the message thread
 * while processBlock() runs on the audio thread.
 */
class InputGainProcessor : public AudioProcessor
{
public:
    InputGainProcessor()
        : AudioProcessor (BusesProperties()
              .withInput  ("Input",  AudioChannelSet::stereo(), true)
              .withOutput ("Output", AudioChannelSet::stereo(), true))
    {
        peaks[0].store (0.f);
        peaks[1].store (0.f);
    }

    void prepareToPlay (double, int) override
    {
        peaks[0].store (0.f);
        peaks[1].store (0.f);
    }
    void releaseResources() override {}

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        float g = gain_.load (std::memory_order_relaxed);
        bool  mono = mono_.load (std::memory_order_relaxed);
        const int numSamples = buffer.getNumSamples();
        const int numCh = jmin (2, buffer.getNumChannels());

        if (mono && numCh >= 2)
        {
            // Apply gain to L, copy to R → centre-panned mono signal
            buffer.applyGain (0, 0, numSamples, g);
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
            float p = buffer.getMagnitude (0, 0, numSamples);
            float cur = peaks[0].load (std::memory_order_relaxed);
            if (p > cur) peaks[0].store (p, std::memory_order_relaxed);
            peaks[1].store (peaks[0].load (std::memory_order_relaxed), std::memory_order_relaxed);
        }
        else
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                buffer.applyGain (ch, 0, numSamples, g);
                float p = buffer.getMagnitude (ch, 0, numSamples);
                float cur = peaks[ch].load (std::memory_order_relaxed);
                if (p > cur) peaks[ch].store (p, std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Atomically read and reset the input peak for channel @p ch.
     * Call from the message thread only.
     * @param ch  0 = Left, 1 = Right.
     * @return    Peak magnitude since the last call.
     */
    float exchangePeak (int ch)
    {
        if (ch < 0 || ch > 1) return 0.f;
        return peaks[ch].exchange (0.f, std::memory_order_relaxed);
    }

    /** @brief Set the input gain. Atomic — safe from any thread. @param g Linear gain. */
    void setGain     (float g) noexcept { gain_.store (g, std::memory_order_relaxed); }

    /** @brief Enable or disable mono fold. Atomic — safe from any thread. @param m true = mono. */
    void setMonoMode (bool m)  noexcept { mono_.store (m, std::memory_order_relaxed); }

    const String getName() const override                { return "LVH Input Gain"; }
    double getTailLengthSeconds() const override         { return 0.0; }
    bool acceptsMidi() const override                    { return false; }
    bool producesMidi() const override                   { return false; }
    bool hasEditor() const override                      { return false; }
    AudioProcessorEditor* createEditor() override        { return nullptr; }
    int getNumPrograms() override                        { return 1; }
    int getCurrentProgram() override                     { return 0; }
    void setCurrentProgram (int) override                {}
    const String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override     {}
    void setStateInformation (const void*, int) override {}

private:
    std::atomic<float> gain_ { 1.0f };
    std::atomic<bool>  mono_ { false };
    std::atomic<float> peaks[2];
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputGainProcessor)
};

// =============================================================================

/**
 * @class AudioEngine
 * @brief Owns the JUCE AudioProcessorGraph and all audio subsystems.
 *
 * AudioEngine is the central audio hub of LVH-PRO.  It manages:
 * - The JUCE AudioProcessorGraph that routes audio between nodes.
 * - Graph configuration switching (Bridge-sync, multi-instrument, sine-wave, direct plugin).
 * - Master output gain and metering (via GainAndMeterProcessor).
 * - Physical audio input gain, mute, and mono fold (via InputGainProcessor).
 * - FFT magnitude and waveform capture for the VisualizerManager.
 * - MIDI keyboard injection and transposition (via MidiInjectionsProcessor).
 * - Metronome click generation (via MetronomeProcessor).
 * - Tap-tempo calculation.
 *
 * **Graph configurations:**
 * | Method | When used |
 * |--------|-----------|
 * | rebuildBridgeGraph() | Normal operation — one or more Bridge subprocesses active |
 * | buildGraphWithBridgeSync() | Legacy single-bridge mode (BridgeSyncProcessor) |
 * | buildGraphWithSineWave() | No plugin loaded; PC keyboard plays a sine-wave tone |
 * | loadPlugin() | Direct JUCE plugin load (no Bridge subprocess) |
 *
 * **Signal path (rebuildBridgeGraph):**
 * @code
 * [Instruments (parallel + per-chan FX)] ─┐
 *                                          ├─→ [Master FX 1] → ... → [Gain/Meter] → [Out]
 * [PhysIn] → [InputGain] → [Input FX] ───┘
 * @endcode
 *
 * **Thread safety:**
 * - All public methods must be called from the message thread,
 *   unless explicitly documented otherwise.
 * - processBlock() runs on the audio thread; it communicates with the
 *   message thread only through atomics and SpinLock-protected buffers.
 * - Pending-state members (pendingGain_, pendingTranspose_, etc.) bridge
 *   the gap between graph rebuilds: values set on the message thread are
 *   applied to the new nodes when the graph is rebuilt.
 */
class AudioEngine
{
public:
    /**
     * @brief Constructs an AudioEngine.
     * @param kbState  The application's MIDI keyboard state.  Must outlive this object.
     */
    explicit AudioEngine (MidiKeyboardState& kbState) : keyboardState (kbState)
    {
        formatManager.addDefaultFormats();
    }

    /**
     * @brief Connect the engine to the audio device and build the initial graph.
     *
     * Registers the AudioProcessorPlayer as the device callback and builds an
     * initial rebuildBridgeGraph() with empty bridge lists so that the physical
     * audio input (LINE IN) is active from startup.
     *
     * @param deviceManager  The application's AudioDeviceManager.
     */
    void initialise (AudioDeviceManager& deviceManager)
    {
        audioProcessorPlayer.setProcessor (&audioGraph);
        deviceManager.addAudioCallback (&audioProcessorPlayer);
        auto& setup = deviceManager.getAudioDeviceSetup();
        lastSampleRate = setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0;
        lastBufferSize = setup.bufferSize > 0   ? setup.bufferSize : 512;
        rebuildBridgeGraph ({}, {});
    }

    /**
     * @brief Disconnect from the audio device and release all graph resources.
     * @param deviceManager  The application's AudioDeviceManager.
     */
    void shutdown (AudioDeviceManager& deviceManager)
    {
        for (auto* slot : slots)
            slot->detach();
        deviceManager.removeAudioCallback (&audioProcessorPlayer);
        audioProcessorPlayer.setProcessor (nullptr);
    }

    /** @brief Returns the JUCE plugin format manager (for plugin scanning). */
    AudioPluginFormatManager& getFormatManager() { return formatManager; }

    /** @brief Returns the AudioProcessorPlayer (for MIDI injection). */
    AudioProcessorPlayer& getPlayer() { return audioProcessorPlayer; }

    // ── Level metering (message thread only) ─────────────────────────────────

    /**
     * @brief Atomically read and reset the master output peak for channel @p ch.
     * @param ch  0 = Left, 1 = Right.
     * @return    Peak magnitude since the last call, or 0 if not available.
     */
    float exchangePeak (int ch)
    {
        return meterGainProcessor ? meterGainProcessor->exchangePeak (ch) : 0.f;
    }

    /**
     * @brief Read the latest IIR-smoothed master output RMS for channel @p ch.
     * @param ch  0 = Left, 1 = Right.
     * @return    Smoothed RMS value, or 0 if not available.
     */
    float exchangeRms (int ch)
    {
        return meterGainProcessor ? meterGainProcessor->exchangeRms (ch) : 0.f;
    }

    // ── FFT / Waveform (message thread or visualizer render thread) ───────────

    /**
     * @brief Fill @p buf with the latest FFT magnitude spectrum.
     *
     * Delegates to GainAndMeterProcessor::readFFTMagnitudes().
     * Returns zeroes if no meter processor is active.
     *
     * @param buf   Output buffer (at least @p size floats).
     * @param size  Number of bins to read (typically 512).
     */
    void readFFTData (float* buf, int size)
    {
        if (meterGainProcessor != nullptr)
            meterGainProcessor->readFFTMagnitudes (buf, size);
        else
            std::fill_n (buf, size, 0.f);
    }

    /**
     * @brief Fill @p buf with the latest time-domain waveform samples.
     *
     * Delegates to GainAndMeterProcessor::readWaveform().
     *
     * @param buf   Output buffer (at least @p size floats).
     * @param size  Number of samples to read (typically 1024).
     */
    void readWaveformData (float* buf, int size)
    {
        if (meterGainProcessor != nullptr)
            meterGainProcessor->readWaveform (buf, size);
        else
            std::fill_n (buf, size, 0.f);
    }

    static constexpr int kFFTBins = GainAndMeterProcessor::fftBins_;  ///< 512 magnitude bins.

    /**
     * @brief Set the master output gain.
     *
     * Thread-safe — can be called from the message thread at any time.
     * The value is also stored as pendingGain so it survives graph rebuilds.
     *
     * @param g  Linear gain (1.0 = unity, 0.0 = silence).
     */
    void setOutputGain (float g)
    {
        pendingGain = g;
        if (meterGainProcessor) meterGainProcessor->setGain (g);
    }

    // ── Graph configuration ───────────────────────────────────────────────────

    /**
     * @brief Switch to Bridge-sync mode for a single bridge subprocess.
     *
     * Replaces the current graph with: [BridgeSyncProc] → [Gain/Meter] → [Out].
     * Used for legacy single-plugin configurations (BridgeSyncProcessor).
     *
     * @param bridgeProc  The BridgeSyncProcessor created by BridgeInstance::createSyncProcessor().
     */
    void buildGraphWithBridgeSync (std::unique_ptr<AudioProcessor> bridgeProc)
    {
        kbProcessor         = nullptr;
        meterGainProcessor  = nullptr;
        metronomeProcessor_ = nullptr;
        getOrCreateSlot().detach();
        audioGraph.clear();

        auto outNode    = audioGraph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
        auto bridgeNode = audioGraph.addNode (std::move (bridgeProc));

        auto* mgProc = new GainAndMeterProcessor();
        mgProc->setGain (pendingGain);
        meterGainProcessor = mgProc;
        auto mgNode    = audioGraph.addNode (std::unique_ptr<GainAndMeterProcessor> (mgProc));
        auto metroNode = addMetronomeNode();

        for (int ch = 0; ch < 2; ++ch)
            audioGraph.addConnection ({{bridgeNode->nodeID, ch}, {mgNode->nodeID, ch}});

        connectToOutput (mgNode, outNode, metroNode);

        if (lastSampleRate > 0.0 && lastBufferSize > 0)
            audioGraph.prepareToPlay (lastSampleRate, lastBufferSize);
    }

    /**
     * @brief Rebuild the audio graph for the current set of active bridges.
     *
     * Constructs the full production signal path:
     * - Instrument bridges are mixed in parallel by MultiSourceBridgeProcessor,
     *   each with an optional per-channel FX chain.
     * - Master effect bridges are chained serially after the instrument mix.
     * - Physical audio input passes through InputGainProcessor and an optional
     *   serial input FX chain before merging into the master chain.
     * - GainAndMeterProcessor is always the final node before audio output.
     *
     * All pending state (gain, input gain, metronome, etc.) is applied to the
     * newly-created nodes so settings survive graph rebuilds.
     *
     * @param instrumentBridges  Bridges with Role::Instrument (mixed in parallel).
     * @param masterEffects      Bridges with Role::Effect in the master chain (serial).
     * @param perChannelFxMap    Per-instrument FX chains (key = instrument bridge).
     * @param inputFxChain       Serial FX applied to physical audio input.
     */
    void rebuildBridgeGraph (
        const juce::Array<BridgeInstance*>& instrumentBridges,
        const juce::Array<BridgeInstance*>& masterEffects,
        const std::map<BridgeInstance*, juce::Array<BridgeInstance*>>& perChannelFxMap = {},
        const juce::Array<BridgeInstance*>& inputFxChain = {})
    {
        kbProcessor         = nullptr;
        meterGainProcessor  = nullptr;
        metronomeProcessor_ = nullptr;
        activeProcessorNodes_.clear();
        getOrCreateSlot().detach();
        audioGraph.clear();

        auto outNode = audioGraph.addNode (
            std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (
                AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

        auto* mgProc = new GainAndMeterProcessor();
        mgProc->setGain (pendingGain);
        meterGainProcessor = mgProc;
        auto mgNode    = audioGraph.addNode (std::unique_ptr<GainAndMeterProcessor> (mgProc));
        auto metroNode = addMetronomeNode();

        AudioProcessorGraph::Node::Ptr lastNode;

        if (! instrumentBridges.isEmpty())
        {
            std::vector<MultiSourceBridgeProcessor::BridgeSource> sources;
            for (auto* b : instrumentBridges)
            {
                MultiSourceBridgeProcessor::BridgeSource src { &b->getSharedMemory(), &b->getSyncEvents(), b };
                auto it = perChannelFxMap.find (b);
                if (it != perChannelFxMap.end())
                    for (auto* fx : it->second)
                        src.fxChain.push_back ({ &fx->getSharedMemory(), &fx->getSyncEvents(), fx });
                sources.push_back (std::move (src));
            }
            lastNode = audioGraph.addNode (
                std::make_unique<MultiSourceBridgeProcessor> (std::move (sources)));
        }

        AudioProcessorGraph::Node::Ptr firstMasterFxNode;
        for (auto* b : masterEffects)
        {
            auto effectNode = audioGraph.addNode (
                std::make_unique<BridgeEffectProcessor> (
                    b->getSharedMemory(), b->getSyncEvents(), b));
            if (lastNode != nullptr)
                for (int ch = 0; ch < 2; ++ch)
                    audioGraph.addConnection ({{lastNode->nodeID, ch}, {effectNode->nodeID, ch}});
            lastNode = effectNode;
            if (firstMasterFxNode == nullptr) firstMasterFxNode = effectNode;
        }

        if (lastNode != nullptr)
            for (int ch = 0; ch < 2; ++ch)
                audioGraph.addConnection ({{lastNode->nodeID, ch}, {mgNode->nodeID, ch}});

        {
            inputGainProcessor_ = nullptr;
            auto inNode = audioGraph.addNode (
                std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (
                    AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
            auto* igProc = new InputGainProcessor();
            igProc->setGain (pendingInputMuted_ ? 0.f : pendingInputGain_);
            igProc->setMonoMode (pendingInputMono_);
            inputGainProcessor_ = igProc;
            auto igNode = audioGraph.addNode (std::unique_ptr<InputGainProcessor> (igProc));
            for (int ch = 0; ch < 2; ++ch)
                audioGraph.addConnection ({{inNode->nodeID, ch}, {igNode->nodeID, ch}});

            AudioProcessorGraph::Node::Ptr inputLastNode = igNode;
            for (auto* b : inputFxChain)
            {
                auto fxNode = audioGraph.addNode (
                    std::make_unique<BridgeEffectProcessor> (
                        b->getSharedMemory(), b->getSyncEvents(), b));
                for (int ch = 0; ch < 2; ++ch)
                    audioGraph.addConnection ({{inputLastNode->nodeID, ch}, {fxNode->nodeID, ch}});
                inputLastNode = fxNode;
            }

            auto& targetNode = (firstMasterFxNode != nullptr) ? firstMasterFxNode : mgNode;
            for (int ch = 0; ch < 2; ++ch)
                audioGraph.addConnection ({{inputLastNode->nodeID, ch}, {targetNode->nodeID, ch}});

            // Insert ProcessorPlugin nodes in parallel with the instrument mix.
            // Each plugin receives the post-input-FX physical audio signal and
            // sums its output into the same targetNode.
            for (int pi = 0; pi < (int)pendingProcessorPlugins_.size(); ++pi)
            {
                auto* rawNode = new ProcessorPluginNode (pendingProcessorPlugins_[(size_t)pi]);

                // Restore pending mixer state so settings survive graph rebuilds.
                if (pi < (int)processorMixerStates_.size())
                {
                    rawNode->mixerGain.store  (processorMixerStates_[(size_t)pi].gain,
                                               std::memory_order_relaxed);
                    rawNode->mixerMuted.store (processorMixerStates_[(size_t)pi].muted,
                                               std::memory_order_relaxed);
                }

                auto  procNode = audioGraph.addNode (
                    std::unique_ptr<ProcessorPluginNode> (rawNode));
                activeProcessorNodes_.push_back (rawNode);

                for (int ch = 0; ch < 2; ++ch)
                    audioGraph.addConnection ({{inputLastNode->nodeID, ch},
                                               {procNode->nodeID,     ch}});
                for (int ch = 0; ch < 2; ++ch)
                    audioGraph.addConnection ({{procNode->nodeID,  ch},
                                               {targetNode->nodeID, ch}});
            }
        }

        connectToOutput (mgNode, outNode, metroNode);

        if (lastSampleRate > 0.0 && lastBufferSize > 0)
            audioGraph.prepareToPlay (lastSampleRate, lastBufferSize);
    }

    /**
     * @brief Build a minimal graph with a SineWaveProcessor for testing.
     *
     * Used when no plugin is loaded.  The PC keyboard plays a sine-wave tone
     * through the MIDI keyboard state.
     */
    void buildGraphWithSineWave()
    {
        kbProcessor         = nullptr;
        meterGainProcessor  = nullptr;
        metronomeProcessor_ = nullptr;
        getOrCreateSlot().detach();
        audioGraph.clear();

        auto outNode  = audioGraph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
        auto midiNode = audioGraph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));
        auto sineNode = audioGraph.addNode (std::make_unique<SineWaveProcessor> (keyboardState));

        auto* mgProc  = new GainAndMeterProcessor();
        mgProc->setGain (pendingGain);
        meterGainProcessor = mgProc;
        auto mgNode   = audioGraph.addNode (std::unique_ptr<GainAndMeterProcessor> (mgProc));
        auto metroNode = addMetronomeNode();

        for (int ch = 0; ch < 2; ++ch)
            audioGraph.addConnection ({{sineNode->nodeID, ch}, {mgNode->nodeID, ch}});
        audioGraph.addConnection ({{midiNode->nodeID, AudioProcessorGraph::midiChannelIndex},
                                   {sineNode->nodeID, AudioProcessorGraph::midiChannelIndex}});

        connectToOutput (mgNode, outNode, metroNode);

        if (lastSampleRate > 0.0 && lastBufferSize > 0)
            audioGraph.prepareToPlay (lastSampleRate, lastBufferSize);
    }

    /**
     * @brief Load a JUCE plugin directly (no Bridge subprocess).
     *
     * Asynchronously creates a plugin instance via the format manager.
     * On success, builds a graph: [MidiIn] → [MidiInject] → [Plugin] → [Gain/Meter] → [Out].
     * On failure, falls back to buildGraphWithSineWave().
     *
     * @param desc        Plugin description from the KnownPluginList.
     * @param sampleRate  Current device sample rate.
     * @param bufferSize  Current device buffer size.
     * @param callback    Called on the message thread with (success, nameOrError).
     */
    void loadPlugin (const PluginDescription& desc, double sampleRate, int bufferSize,
                     std::function<void(bool success, const String& nameOrError)> callback)
    {
        if (sampleRate > 0.0) lastSampleRate = sampleRate;
        if (bufferSize > 0)   lastBufferSize  = bufferSize;
        getOrCreateSlot().detach();

        formatManager.createPluginInstanceAsync (desc, sampleRate, bufferSize,
            [this, sampleRate, bufferSize, cb = std::move (callback)]
            (std::unique_ptr<AudioPluginInstance> instance, const String& error)
            {
                if (instance == nullptr)
                {
                    buildGraphWithSineWave();
                    cb (false, error);
                    return;
                }

                kbProcessor         = nullptr;
                meterGainProcessor  = nullptr;
                metronomeProcessor_ = nullptr;
                auto* rawPtr = instance.get();
                audioGraph.clear();

                auto outNode  = audioGraph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
                auto midiNode = audioGraph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));

                auto* kbProc  = new MidiInjectionsProcessor (keyboardState);
                kbProc->setTranspose     (pendingTranspose);
                kbProc->setChannelFilter (pendingChannel);
                kbProcessor   = kbProc;
                auto kbNode   = audioGraph.addNode (std::unique_ptr<MidiInjectionsProcessor> (kbProc));
                auto plugNode = audioGraph.addNode (std::move (instance));

                auto* mgProc  = new GainAndMeterProcessor();
                mgProc->setGain (pendingGain);
                meterGainProcessor = mgProc;
                auto mgNode    = audioGraph.addNode (std::unique_ptr<GainAndMeterProcessor> (mgProc));
                auto metroNode = addMetronomeNode();

                for (int ch = 0; ch < 2; ++ch)
                    audioGraph.addConnection ({{plugNode->nodeID, ch}, {mgNode->nodeID, ch}});

                audioGraph.addConnection ({{midiNode->nodeID, AudioProcessorGraph::midiChannelIndex},
                                           {plugNode->nodeID, AudioProcessorGraph::midiChannelIndex}});
                audioGraph.addConnection ({{kbNode->nodeID,   AudioProcessorGraph::midiChannelIndex},
                                           {plugNode->nodeID, AudioProcessorGraph::midiChannelIndex}});

                connectToOutput (mgNode, outNode, metroNode);

                audioGraph.prepareToPlay (sampleRate, bufferSize);
                getOrCreateSlot().attach (rawPtr, plugNode->nodeID);
                cb (true, rawPtr->getName());
            });
    }

    /** @brief Unload the directly loaded plugin and fall back to the sine-wave graph. */
    void unloadPlugin()
    {
        getOrCreateSlot().detach();
        buildGraphWithSineWave();
    }

    /** @brief Returns true if a plugin is loaded in the direct (non-Bridge) slot. */
    bool isPluginLoaded() const { return ! slots.isEmpty() && slots[0]->isLoaded(); }

    // ── Processor plugins (message thread) ───────────────────────────────────

    /**
     * @brief Set the list of processor plugins to include in every subsequent
     *        rebuildBridgeGraph() call.
     *
     * Each plugin is wrapped in a ProcessorPluginNode and inserted into the
     * audio graph.  The pointers must remain valid for the lifetime of the
     * engine (ProcessorManager owns them).
     *
     * Triggers an immediate rebuildBridgeGraph() with empty bridge lists so
     * that the plugins are active before any bridge connects.
     *
     * @param plugins  Raw pointers to loaded IProcessorPlugin instances.
     */
    void setProcessorPlugins (std::vector<IProcessorPlugin*> plugins)
    {
        processorMixerStates_.resize (plugins.size());  // preserves existing values, fills new with defaults
        pendingProcessorPlugins_ = std::move (plugins);
        rebuildBridgeGraph ({}, {});
    }

    /**
     * @brief Set the output gain for a processor plugin strip.
     * @param idx   Index into the processor plugin list (same order as setProcessorPlugins).
     * @param gain  Linear gain value [0.0, 1.5].
     */
    void setProcessorGain (int idx, float gain)
    {
        if (idx >= 0 && idx < (int)processorMixerStates_.size())
            processorMixerStates_[(size_t)idx].gain = gain;
        if (idx >= 0 && idx < (int)activeProcessorNodes_.size())
            if (auto* n = activeProcessorNodes_[(size_t)idx])
                n->mixerGain.store (gain, std::memory_order_relaxed);
    }

    /**
     * @brief Mute or unmute a processor plugin strip.
     * @param idx    Index into the processor plugin list.
     * @param muted  true to silence the output.
     */
    void setProcessorMuted (int idx, bool muted)
    {
        if (idx >= 0 && idx < (int)processorMixerStates_.size())
            processorMixerStates_[(size_t)idx].muted = muted;
        if (idx >= 0 && idx < (int)activeProcessorNodes_.size())
            if (auto* n = activeProcessorNodes_[(size_t)idx])
                n->mixerMuted.store (muted, std::memory_order_relaxed);
    }

    /**
     * @brief Atomically read and reset the output peak for a processor plugin.
     *
     * @param processorIdx  Index into the last-built processor node list.
     * @param ch            0 = Left, 1 = Right.
     * @return              Peak magnitude since the last call, or 0 if unavailable.
     */
    float exchangeProcessorPeak (int processorIdx, int ch) noexcept
    {
        if (processorIdx >= 0 && processorIdx < (int)activeProcessorNodes_.size())
            if (auto* node = activeProcessorNodes_[(size_t)processorIdx])
                return node->exchangePeak (ch);
        return 0.f;
    }

    /** @brief Returns the number of processor plugin nodes in the current graph. */
    int getNumActiveProcessorNodes() const noexcept
    {
        return (int)activeProcessorNodes_.size();
    }

    /**
     * @brief Returns the PluginSlot at @p index, or nullptr if out of range.
     * @param index  Zero-based slot index.
     */
    PluginSlot* getSlot (int index = 0)
    {
        return slots.size() > index ? slots[index] : nullptr;
    }

    /** @brief Returns the total graph latency in samples (sum of all slots). */
    int calculateTotalLatency() const
    {
        int total = 0;
        for (auto* slot : slots) total += slot->getLatencyInSamples();
        return total;
    }

    /**
     * @brief Send All Notes Off / All Sound Off to all MIDI channels.
     *
     * Clears the keyboard state, sets panic mode on the MIDI injections processor,
     * and sends CC 120 (All Sound Off), CC 123 (All Notes Off), CC 121 (Reset All
     * Controllers), and CC 64 (Sustain Off) on all 16 MIDI channels.
     */
    void allNotesOff()
    {
        keyboardState.allNotesOff (0);
        if (kbProcessor != nullptr) kbProcessor->setPanicMode();
        for (int ch = 1; ch <= 16; ++ch)
        {
            audioProcessorPlayer.handleIncomingMidiMessage (nullptr, MidiMessage::allSoundOff (ch));
            audioProcessorPlayer.handleIncomingMidiMessage (nullptr, MidiMessage::allNotesOff (ch));
            audioProcessorPlayer.handleIncomingMidiMessage (nullptr, MidiMessage::controllerEvent (ch, 121, 0));
            audioProcessorPlayer.handleIncomingMidiMessage (nullptr, MidiMessage::controllerEvent (ch, 64, 0));
        }
    }

    /**
     * @brief Update the cached sample rate and buffer size after a device change.
     * @param sr  New sample rate (ignored if <= 0).
     * @param bs  New buffer size (ignored if <= 0).
     */
    void updateConfig (double sr, int bs)
    {
        if (sr > 0.0) lastSampleRate = sr;
        if (bs > 0)   lastBufferSize  = bs;
    }

    // ── Physical input control (message thread) ───────────────────────────────

    /**
     * @brief Atomically read and reset the physical input peak for channel @p ch.
     * @param ch  0 = Left, 1 = Right.
     * @return    Peak magnitude since the last call, or 0 if unavailable.
     */
    float exchangeInputPeak (int ch)
    {
        return inputGainProcessor_ ? inputGainProcessor_->exchangePeak (ch) : 0.f;
    }

    /**
     * @brief Set the physical audio input gain.
     *
     * Has no effect when input is muted (mute takes priority).
     * @param g  Linear gain value.
     */
    void setInputGain (float g)
    {
        pendingInputGain_ = g;
        if (inputGainProcessor_ && ! pendingInputMuted_)
            inputGainProcessor_->setGain (g);
    }

    /**
     * @brief Mute or unmute the physical audio input.
     * @param muted  true to silence the input.
     */
    void setInputMuted (bool muted)
    {
        pendingInputMuted_ = muted;
        if (inputGainProcessor_)
            inputGainProcessor_->setGain (muted ? 0.f : pendingInputGain_);
    }

    /**
     * @brief Enable or disable mono fold for the physical audio input.
     * @param mono  true to sum L+R to mono.
     */
    void setInputMono (bool mono)
    {
        pendingInputMono_ = mono;
        if (inputGainProcessor_) inputGainProcessor_->setMonoMode (mono);
    }

    /** @brief Returns the current physical input gain. */
    float getInputGain()  const noexcept { return pendingInputGain_; }
    /** @brief Returns true if the physical input is muted. */
    bool  isInputMuted()  const noexcept { return pendingInputMuted_; }
    /** @brief Returns true if the physical input is in mono fold mode. */
    bool  isInputMono()   const noexcept { return pendingInputMono_; }

    /**
     * @brief Set the MIDI transpose amount applied to all keyboard input.
     * @param semitones  Number of semitones to shift (positive = up).
     */
    void setTranspose (int semitones)
    {
        pendingTranspose = semitones;
        if (kbProcessor != nullptr) kbProcessor->setTranspose (semitones);
    }

    /**
     * @brief Filter MIDI input to a single channel (0 = all channels pass).
     * @param channel  MIDI channel 1–16, or 0 for omni.
     */
    void setChannelFilter (int channel)
    {
        pendingChannel = channel;
        if (kbProcessor != nullptr) kbProcessor->setChannelFilter (channel);
    }

    // ── Metronome API (message thread) ────────────────────────────────────────

    /** @brief Start or stop the metronome click. @param p true = playing. */
    void setMetronomePlaying (bool p)
    {
        pendingMetroPlaying_ = p;
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->setPlaying (p);
    }

    /** @brief Set the metronome tempo. @param bpm Beats per minute. */
    void setMetronomeBpm (double bpm)
    {
        pendingMetroBpm_ = bpm;
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->setBpm (bpm);
    }

    /** @brief Set the metronome click volume. @param v Linear volume [0.0, 1.0]. */
    void setMetronomeVolume (float v)
    {
        pendingMetroVolume_ = v;
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->setVolume (v);
    }

    /** @brief Set the time signature numerator. @param b Beats per bar (e.g. 4). */
    void setMetronomeBeatsPerBar (int b)
    {
        pendingMetroBeatsPerBar_ = b;
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->setBeatsPerBar (b);
    }

    /** @brief Set the click sound style. @param t Normal or Techno. */
    void setMetronomeClickType (MetronomeProcessor::ClickType t)
    {
        pendingMetroClickType_ = t;
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->setClickType (t);
    }

    /** @brief Returns true if the metronome is currently playing. */
    bool   isMetronomePlaying()      const noexcept { return pendingMetroPlaying_; }
    /** @brief Returns the current metronome tempo in BPM. */
    double getMetronomeBpm()         const noexcept { return pendingMetroBpm_; }
    /** @brief Returns the metronome click volume. */
    float  getMetronomeVolume()      const noexcept { return pendingMetroVolume_; }
    /** @brief Returns the beats-per-bar setting. */
    int    getMetronomeBeatsPerBar() const noexcept { return pendingMetroBeatsPerBar_; }
    /** @brief Returns the click sound style. */
    MetronomeProcessor::ClickType getMetronomeClickType() const noexcept { return pendingMetroClickType_; }

    /**
     * @brief Wire the beat callback fired on each metronome click.
     *
     * Called by MetronomeManager after construction.  Also applied to the
     * current MetronomeProcessor node if one exists.
     *
     * @param cb  Callback receiving the beat number within the bar (1-based).
     */
    void setMetronomeOnBeat (std::function<void(int)> cb)
    {
        metroOnBeat_ = std::move (cb);
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->onBeat = metroOnBeat_;
    }

    /**
     * @brief Register a tap and update the BPM from the average tap interval.
     *
     * Keeps up to 8 taps within a 3-second window.  Requires at least 2 taps
     * before updating the BPM.  Clamps output to [40, 240] BPM.
     */
    void tapMetronomeTempo()
    {
        const juce::int64 now = juce::Time::currentTimeMillis();
        tapTimes_.push_back (now);
        while (tapTimes_.size() > 8
               || (tapTimes_.size() > 1 && now - tapTimes_.front() > 3000LL))
            tapTimes_.pop_front();
        if (tapTimes_.size() < 2) return;
        double totalMs = static_cast<double> (tapTimes_.back() - tapTimes_.front());
        double avgMs   = totalMs / static_cast<double> (tapTimes_.size() - 1);
        setMetronomeBpm (juce::jlimit (40.0, 240.0, 60000.0 / avgMs));
    }

private:
    PluginSlot& getOrCreateSlot (int index = 0)
    {
        while (slots.size() <= index)
            slots.add (new PluginSlot());
        return *slots[index];
    }

    /// Create a MetronomeProcessor node, apply all pending metronome state, and return it.
    AudioProcessorGraph::Node::Ptr addMetronomeNode()
    {
        auto* mp = new MetronomeProcessor();
        mp->setPlaying    (pendingMetroPlaying_);
        mp->setBpm        (pendingMetroBpm_);
        mp->setVolume     (pendingMetroVolume_);
        mp->setBeatsPerBar(pendingMetroBeatsPerBar_);
        mp->setClickType  (pendingMetroClickType_);
        mp->onBeat        = metroOnBeat_;
        metronomeProcessor_ = mp;
        return audioGraph.addNode (std::unique_ptr<MetronomeProcessor> (mp));
    }

    /// Connect fromNode → metroNode → outNode for both stereo channels.
    void connectToOutput (AudioProcessorGraph::Node::Ptr fromNode,
                          AudioProcessorGraph::Node::Ptr outNode,
                          AudioProcessorGraph::Node::Ptr metroNode)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            audioGraph.addConnection ({{fromNode->nodeID,  ch}, {metroNode->nodeID, ch}});
            audioGraph.addConnection ({{metroNode->nodeID, ch}, {outNode->nodeID,   ch}});
        }
    }

    MidiKeyboardState&        keyboardState;
    AudioProcessorGraph       audioGraph;
    AudioProcessorPlayer      audioProcessorPlayer;
    AudioPluginFormatManager  formatManager;
    OwnedArray<PluginSlot>    slots;

    /// Raw pointers into nodes owned by audioGraph.  Invalidated on every graph rebuild.
    MidiInjectionsProcessor*         kbProcessor          = nullptr;
    GainAndMeterProcessor*           meterGainProcessor   = nullptr;
    MetronomeProcessor*              metronomeProcessor_  = nullptr;
    InputGainProcessor*              inputGainProcessor_  = nullptr;
    std::vector<ProcessorPluginNode*> activeProcessorNodes_;   ///< Parallel to pendingProcessorPlugins_.

    // Pending state — survives graph rebuilds and is applied to new nodes.
    std::vector<IProcessorPlugin*> pendingProcessorPlugins_;   ///< Set via setProcessorPlugins().

    struct ProcessorMixerState { float gain = 1.0f; bool muted = false; };
    std::vector<ProcessorMixerState> processorMixerStates_;    ///< Per-plugin mixer state (parallel to pendingProcessorPlugins_).

    float  pendingGain        = 1.0f;
    int    pendingTranspose    = 0;
    int    pendingChannel      = 0;
    double lastSampleRate      = 0.0;
    int    lastBufferSize      = 0;
    float  pendingInputGain_   = 1.0f;
    bool   pendingInputMuted_  = false;
    bool   pendingInputMono_   = false;

    bool   pendingMetroPlaying_     = false;
    double pendingMetroBpm_         = 120.0;
    float  pendingMetroVolume_      = 0.7f;
    int    pendingMetroBeatsPerBar_ = 4;
    MetronomeProcessor::ClickType pendingMetroClickType_ = MetronomeProcessor::ClickType::Normal;
    std::function<void(int)> metroOnBeat_;
    std::deque<juce::int64>  tapTimes_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
