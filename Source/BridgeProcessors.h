#pragma once
// BridgeInstance.h includes BridgeSyncProcessor.h (which cannot include BridgeInstance.h).
// This file is the correct place for processors that need full BridgeInstance access.
#include "BridgeInstance.h"

// =====================================================================
// MultiSourceBridgeProcessor
//
// Signals ALL instrument bridges simultaneously (parallel), waits for
// each, and sums their outputs with per-channel mixer state applied:
//   - Fader gain, stereo pan, mute, and solo (DAW-style)
//   - Post-fader peak tracking written back to BridgeInstance atomics
// =====================================================================
class MultiSourceBridgeProcessor : public juce::AudioProcessor
{
public:
    // 035-A: Release timeout raised from 15ms to 30ms (same reasoning as BridgeSyncProcessor).
   #if JUCE_DEBUG
    static constexpr int kProcessTimeoutMs = 200;
   #else
    static constexpr int kProcessTimeoutMs = 30;
   #endif

    struct FxEntry
    {
        SharedMemoryBuffer* shm;
        SyncEvents*         events;
        BridgeInstance*     bridge = nullptr;  // for bypass check
    };

    struct BridgeSource
    {
        SharedMemoryBuffer* shm;
        SyncEvents*         events;
        BridgeInstance*     bridge = nullptr;  // for mixer state + peak reporting (nullable)
        std::vector<FxEntry> fxChain;          // per-channel FX, applied before mixing
    };

    explicit MultiSourceBridgeProcessor (std::vector<BridgeSource> sources)
        : juce::AudioProcessor (BusesProperties()
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
        , sources_ (std::move (sources))
    {}

    void prepareToPlay (double sampleRate, int maxBlockSize) override
    {
        for (auto& src : sources_)
        {
            if (auto* layout = src.shm->getLayout())
            {
                layout->sampleRate  = static_cast<float> (sampleRate);
                layout->bufferSize  = maxBlockSize;
                layout->numChannels = 2;
            }
            for (auto& fx : src.fxChain)
                if (auto* layout = fx.shm->getLayout())
                {
                    layout->sampleRate  = static_cast<float> (sampleRate);
                    layout->bufferSize  = maxBlockSize;
                    layout->numChannels = 2;
                }
        }
        // Pre-allocate temp buffer for per-channel FX processing (avoids audio-thread allocation)
        tmpBuffer_.setSize (2, maxBlockSize, false, true, true);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();
        const int numSamples  = juce::jmin (buffer.getNumSamples(), 4096);
        const int numChannels = juce::jmin (2, buffer.getNumChannels());

        // Determine if any source is soloed (DAW-style solo logic)
        bool anySoloed = false;
        for (auto& src : sources_)
            if (src.bridge != nullptr && src.bridge->mixerSoloed.load (std::memory_order_relaxed))
                { anySoloed = true; break; }

        // Step 1: Signal ALL bridges simultaneously (even muted — they must keep running)
        for (auto& src : sources_)
            if (src.events->isOpen())
                src.events->signalRequest();

        // Step 2: Wait for each bridge, run per-channel FX chain, apply mixer state, accumulate
        for (auto& src : sources_)
        {
            if (! src.events->isOpen()) continue;
            if (! src.events->waitForDone (kProcessTimeoutMs)) continue;

            auto* layout = src.shm->getLayout();
            if (layout == nullptr) continue;

            // Copy instrument output into tmpBuffer for FX chain processing
            if (tmpBuffer_.getNumSamples() >= numSamples)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    std::memcpy (tmpBuffer_.getWritePointer (ch),
                                 layout->audioOut[ch],
                                 (size_t) numSamples * sizeof (float));
            }

            // Run per-channel FX chain serially on tmpBuffer
            for (auto& fx : src.fxChain)
            {
                if (! fx.events->isOpen()) continue;
                // Bypass check
                if (fx.bridge != nullptr && fx.bridge->mixerBypassed.load (std::memory_order_relaxed))
                    continue;

                auto* fxLayout = fx.shm->getLayout();
                if (fxLayout == nullptr) continue;

                // Write tmpBuffer into FX bridge's audioIn
                for (int ch = 0; ch < numChannels; ++ch)
                    std::memcpy (fxLayout->audioIn[ch],
                                 tmpBuffer_.getReadPointer (ch),
                                 (size_t) numSamples * sizeof (float));

                fx.events->signalRequest();
                if (fx.events->waitForDone (kProcessTimeoutMs))
                    for (int ch = 0; ch < numChannels; ++ch)
                        std::memcpy (tmpBuffer_.getWritePointer (ch),
                                     fxLayout->audioOut[ch],
                                     (size_t) numSamples * sizeof (float));
                // On timeout: tmpBuffer remains unchanged (dry passthrough for this FX)
            }

            // Determine whether this channel should be heard
            bool active = true;
            if (src.bridge != nullptr)
            {
                if (src.bridge->mixerMuted.load (std::memory_order_relaxed))
                    active = false;
                else if (anySoloed && ! src.bridge->mixerSoloed.load (std::memory_order_relaxed))
                    active = false;
            }

            // Per-channel gain from fader × linear pan law
            float gain = src.bridge ? src.bridge->mixerGain.load (std::memory_order_relaxed) : 1.0f;
            float pan  = src.bridge ? src.bridge->mixerPan.load  (std::memory_order_relaxed) : 0.0f;
            float gainL = gain * (pan <= 0.f ? 1.0f : (1.0f - pan));
            float gainR = gain * (pan >= 0.f ? 1.0f : (1.0f + pan));

            float runPeakL = 0.f, runPeakR = 0.f;

            if (active && numChannels >= 1)
            {
                auto* outL = buffer.getWritePointer (0);
                auto* outR = (numChannels >= 2) ? buffer.getWritePointer (1) : nullptr;

                for (int i = 0; i < numSamples; ++i)
                {
                    float l = tmpBuffer_.getReadPointer (0)[i] * gainL;
                    float r = (numChannels >= 2) ? tmpBuffer_.getReadPointer (1)[i] * gainR : l;
                    outL[i] += l;
                    if (outR != nullptr) outR[i] += r;

                    float al = std::abs (l), ar = std::abs (r);
                    if (al > runPeakL) runPeakL = al;
                    if (ar > runPeakR) runPeakR = ar;
                }
            }

            // Update bridge peak atomics (run-and-max; audio thread only)
            if (src.bridge != nullptr)
            {
                float curL = src.bridge->peakL.load (std::memory_order_relaxed);
                float curR = src.bridge->peakR.load (std::memory_order_relaxed);
                if (runPeakL > curL) src.bridge->peakL.store (runPeakL, std::memory_order_relaxed);
                if (runPeakR > curR) src.bridge->peakR.store (runPeakR, std::memory_order_relaxed);
            }
        }
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::disabled();
    }

    const juce::String getName() const override                { return "MultiSourceBridgeProcessor"; }
    double getTailLengthSeconds() const override               { return 0.0; }
    bool acceptsMidi() const override                          { return false; }
    bool producesMidi() const override                         { return false; }
    bool hasEditor() const override                            { return false; }
    juce::AudioProcessorEditor* createEditor() override        { return nullptr; }
    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override     {}
    void setStateInformation (const void*, int) override       {}

private:
    std::vector<BridgeSource>    sources_;
    juce::AudioBuffer<float>     tmpBuffer_;  // pre-allocated; used for per-channel FX chain processing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiSourceBridgeProcessor)
};

// =====================================================================
// BridgeEffectProcessor
//
// Like BridgeSyncProcessor but with an audio INPUT bus, for VST3 effects
// in a serial effect chain.
// =====================================================================
class BridgeEffectProcessor : public juce::AudioProcessor
{
public:
    // 035-A: Release timeout raised from 15ms to 30ms.
   #if JUCE_DEBUG
    static constexpr int kProcessTimeoutMs = 200;
   #else
    static constexpr int kProcessTimeoutMs = 30;
   #endif

    BridgeEffectProcessor (SharedMemoryBuffer& shm, SyncEvents& events,
                           BridgeInstance* bridge = nullptr)
        : juce::AudioProcessor (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
        , shm (shm), events (events), bridge_ (bridge)
    {}

    void prepareToPlay (double sampleRate, int maxBlockSize) override
    {
        if (auto* layout = shm.getLayout())
        {
            layout->sampleRate  = static_cast<float> (sampleRate);
            layout->bufferSize  = maxBlockSize;
            layout->numChannels = 2;
        }
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        // Bypass: leave the input buffer unchanged (passthrough)
        if (bridge_ != nullptr && bridge_->mixerBypassed.load (std::memory_order_relaxed))
            return;

        auto* layout = shm.getLayout();
        if (layout == nullptr || ! events.isOpen())
            return;

        const int numSamples  = juce::jmin (buffer.getNumSamples(), 4096);
        const int numChannels = juce::jmin (2, buffer.getNumChannels());

        for (int ch = 0; ch < numChannels; ++ch)
            std::memcpy (layout->audioIn[ch], buffer.getReadPointer (ch),
                         (size_t) numSamples * sizeof (float));

        events.signalRequest();

        if (events.waitForDone (kProcessTimeoutMs))
            for (int ch = 0; ch < numChannels; ++ch)
                std::memcpy (buffer.getWritePointer (ch), layout->audioOut[ch],
                             (size_t) numSamples * sizeof (float));
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
    }

    const juce::String getName() const override                { return "BridgeEffectProcessor"; }
    double getTailLengthSeconds() const override               { return 0.0; }
    bool acceptsMidi() const override                          { return false; }
    bool producesMidi() const override                         { return false; }
    bool hasEditor() const override                            { return false; }
    juce::AudioProcessorEditor* createEditor() override        { return nullptr; }
    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override     {}
    void setStateInformation (const void*, int) override       {}

private:
    SharedMemoryBuffer& shm;
    SyncEvents&         events;
    BridgeInstance*     bridge_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeEffectProcessor)
};
