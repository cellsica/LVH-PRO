#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
#include <atomic>
#include <map>
#include "SineWaveProcessor.h"
#include "PluginSlot.h"
#include "MidiInjectionsProcessor.h"
#include "../BridgeProcessors.h"
#include "../MetronomeManager.h"

using namespace juce;

// =========================================================================
// GainAndMeterProcessor
// Inserted as a graph node just before the AudioOutput node.
// Applies master output gain and captures peak L/R levels — all on the
// audio thread using only atomic operations (no locks needed).
// =========================================================================
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
    }

    // Call from the message thread only — returns peak since last call and resets.
    float exchangePeak (int ch)
    {
        if (ch < 0 || ch > 1) return 0.f;
        return peaks[ch].exchange (0.f, std::memory_order_relaxed);
    }

    // Call from the message thread only — returns latest smoothed RMS.
    // Uses load (not exchange) so the physics engine always sees the current
    // IIR-smoothed value regardless of timer/audio-block timing.
    float exchangeRms (int ch)
    {
        if (ch < 0 || ch > 1) return 0.f;
        return rms[ch].load (std::memory_order_relaxed);
    }

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

    // RMS (VU meter)
    std::atomic<float> rms[2];
    float rmsSmoothed[2] = {};  // IIR state — audio thread only
    float rmsAlpha       = 0.1f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainAndMeterProcessor)
};

// =========================================================================
// AudioEngine
// =========================================================================
class AudioEngine
{
public:
    explicit AudioEngine (MidiKeyboardState& kbState) : keyboardState (kbState)
    {
        formatManager.addDefaultFormats();
    }

    void initialise (AudioDeviceManager& deviceManager)
    {
        audioProcessorPlayer.setProcessor (&audioGraph);
        deviceManager.addAudioCallback (&audioProcessorPlayer);  // single callback — no mixing issues

        // Cache the device's sample rate and buffer size so buildGraphWithSineWave() can
        // call prepareToPlay even when no PluginDescription is available.
        auto& setup = deviceManager.getAudioDeviceSetup();
        lastSampleRate = setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0;
        lastBufferSize = setup.bufferSize > 0   ? setup.bufferSize : 512;
    }

    void shutdown (AudioDeviceManager& deviceManager)
    {
        for (auto* slot : slots)
            slot->detach();
        deviceManager.removeAudioCallback (&audioProcessorPlayer);
        audioProcessorPlayer.setProcessor (nullptr);
    }

    AudioPluginFormatManager& getFormatManager() { return formatManager; }
    AudioProcessorPlayer& getPlayer()             { return audioProcessorPlayer; }

    // Peak levels — call from message thread only.
    float exchangePeak (int ch)
    {
        return meterGainProcessor ? meterGainProcessor->exchangePeak (ch) : 0.f;
    }

    // RMS levels for VU meter — call from message thread only.
    float exchangeRms (int ch)
    {
        return meterGainProcessor ? meterGainProcessor->exchangeRms (ch) : 0.f;
    }

    // Master output gain — thread-safe.
    void setOutputGain (float g)
    {
        pendingGain = g;
        if (meterGainProcessor) meterGainProcessor->setGain (g);
    }

    /** Switch Core's audio graph to Bridge-sync mode.
        The supplied processor (BridgeSyncProcessor) replaces the local plugin.
        Call this when the Bridge process connects and is ready. */
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

    /** Rebuild Core's audio graph for serial effect-chain routing.
        Instruments are processed in parallel (MultiSourceBridgeProcessor), each with
        its own per-channel FX chain (perChannelFxMap), then the mixed output is fed
        through each master effect bridge in order.
        Pass empty arrays to fall back to the sine-wave generator.
        Graph: [Instr(parallel+per-chan FX)] → [MasterFX1] → [MasterFX2] → ... → [Gain/Meter] → [Out] */
    void rebuildBridgeGraph (
        const juce::Array<BridgeInstance*>& instrumentBridges,
        const juce::Array<BridgeInstance*>& masterEffects,
        const std::map<BridgeInstance*, juce::Array<BridgeInstance*>>& perChannelFxMap = {})
    {
        if (instrumentBridges.isEmpty() && masterEffects.isEmpty())
        {
            buildGraphWithSineWave();
            return;
        }

        kbProcessor         = nullptr;
        meterGainProcessor  = nullptr;
        metronomeProcessor_ = nullptr;
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

        // Build signal chain: instruments (parallel mix + per-chan FX) → master effects (serial) → gain/meter
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

        // Physical audio input — always active when bridges are connected.
        // Signal is summed into the first master FX node (or gain/meter if no master FX).
        // JUCE's AudioProcessorGraph automatically sums multiple connections to the same input.
        {
            auto inNode = audioGraph.addNode (
                std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (
                    AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
            auto& targetNode = (firstMasterFxNode != nullptr) ? firstMasterFxNode : mgNode;
            for (int ch = 0; ch < 2; ++ch)
                audioGraph.addConnection ({{inNode->nodeID, ch}, {targetNode->nodeID, ch}});
        }

        connectToOutput (mgNode, outNode, metroNode);

        if (lastSampleRate > 0.0 && lastBufferSize > 0)
            audioGraph.prepareToPlay (lastSampleRate, lastBufferSize);
    }

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

    void unloadPlugin()
    {
        getOrCreateSlot().detach();
        buildGraphWithSineWave();
    }

    bool isPluginLoaded() const { return ! slots.isEmpty() && slots[0]->isLoaded(); }

    PluginSlot* getSlot (int index = 0)
    {
        return slots.size() > index ? slots[index] : nullptr;
    }

    int calculateTotalLatency() const
    {
        int total = 0;
        for (auto* slot : slots) total += slot->getLatencyInSamples();
        return total;
    }

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

    void updateConfig (double sr, int bs)
    {
        if (sr > 0.0) lastSampleRate = sr;
        if (bs > 0)   lastBufferSize  = bs;
    }

    void setTranspose (int semitones)
    {
        pendingTranspose = semitones;
        if (kbProcessor != nullptr) kbProcessor->setTranspose (semitones);
    }

    void setChannelFilter (int channel)
    {
        pendingChannel = channel;
        if (kbProcessor != nullptr) kbProcessor->setChannelFilter (channel);
    }

    // ── Metronome API (message thread) ────────────────────────────────────

    void setMetronomePlaying (bool p)
    {
        pendingMetroPlaying_ = p;
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->setPlaying (p);
    }

    void setMetronomeBpm (double bpm)
    {
        pendingMetroBpm_ = bpm;
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->setBpm (bpm);
    }

    void setMetronomeVolume (float v)
    {
        pendingMetroVolume_ = v;
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->setVolume (v);
    }

    void setMetronomeBeatsPerBar (int b)
    {
        pendingMetroBeatsPerBar_ = b;
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->setBeatsPerBar (b);
    }

    void setMetronomeClickType (MetronomeProcessor::ClickType t)
    {
        pendingMetroClickType_ = t;
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->setClickType (t);
    }

    bool   isMetronomePlaying()   const noexcept { return pendingMetroPlaying_; }
    double getMetronomeBpm()      const noexcept { return pendingMetroBpm_; }
    float  getMetronomeVolume()   const noexcept { return pendingMetroVolume_; }
    int    getMetronomeBeatsPerBar()    const noexcept { return pendingMetroBeatsPerBar_; }
    MetronomeProcessor::ClickType getMetronomeClickType() const noexcept { return pendingMetroClickType_; }

    // Wire the beat callback. Called by MetronomeManager after construction.
    void setMetronomeOnBeat (std::function<void(int)> cb)
    {
        metroOnBeat_ = std::move (cb);
        if (metronomeProcessor_ != nullptr)
            metronomeProcessor_->onBeat = metroOnBeat_;
    }

    // Tap tempo: call on every tap; updates BPM from average interval.
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

    // Create a MetronomeProcessor node, apply pending state, and return it.
    // Also stores the raw ptr in metronomeProcessor_.
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

    // Connect fromNode → [metroNode] → outNode for both stereo channels.
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

    MidiKeyboardState& keyboardState;
    AudioProcessorGraph audioGraph;
    AudioProcessorPlayer audioProcessorPlayer;
    AudioPluginFormatManager formatManager;
    OwnedArray<PluginSlot> slots;
    MidiInjectionsProcessor*  kbProcessor         = nullptr; // raw ptr; owned by audioGraph
    GainAndMeterProcessor*    meterGainProcessor   = nullptr; // raw ptr; owned by audioGraph
    MetronomeProcessor*       metronomeProcessor_  = nullptr; // raw ptr; owned by audioGraph

    float  pendingGain    = 1.0f;
    int    pendingTranspose = 0;
    int    pendingChannel   = 0;
    double lastSampleRate   = 0.0;
    int    lastBufferSize   = 0;

    // Metronome pending state (survives graph rebuilds)
    bool   pendingMetroPlaying_     = false;
    double pendingMetroBpm_         = 120.0;
    float  pendingMetroVolume_      = 0.7f;
    int    pendingMetroBeatsPerBar_ = 4;
    MetronomeProcessor::ClickType pendingMetroClickType_ = MetronomeProcessor::ClickType::Normal;
    std::function<void(int)>    metroOnBeat_;
    std::deque<juce::int64>     tapTimes_;     // tap tempo history

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
