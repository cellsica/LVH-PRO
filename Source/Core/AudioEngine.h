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
    }

    void prepareToPlay (double, int) override
    {
        peaks[0].store (0.f);
        peaks[1].store (0.f);
    }
    void releaseResources() override {}

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        float g = gain.load (std::memory_order_relaxed);
        for (int ch = 0; ch < jmin (2, buffer.getNumChannels()); ++ch)
        {
            buffer.applyGain (ch, 0, buffer.getNumSamples(), g);
            float p = buffer.getMagnitude (ch, 0, buffer.getNumSamples());
            float cur = peaks[ch].load (std::memory_order_relaxed);
            if (p > cur) peaks[ch].store (p, std::memory_order_relaxed);
        }
    }

    // Call from the message thread only — returns peak since last call and resets.
    float exchangePeak (int ch)
    {
        if (ch < 0 || ch > 1) return 0.f;
        return peaks[ch].exchange (0.f, std::memory_order_relaxed);
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
        kbProcessor        = nullptr;
        meterGainProcessor = nullptr;
        getOrCreateSlot().detach();
        audioGraph.clear();

        auto outNode    = audioGraph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
        auto bridgeNode = audioGraph.addNode (std::move (bridgeProc));

        auto* mgProc = new GainAndMeterProcessor();
        mgProc->setGain (pendingGain);
        meterGainProcessor = mgProc;
        auto mgNode = audioGraph.addNode (std::unique_ptr<GainAndMeterProcessor> (mgProc));

        for (int ch = 0; ch < 2; ++ch)
        {
            audioGraph.addConnection ({{bridgeNode->nodeID, ch}, {mgNode->nodeID,  ch}});
            audioGraph.addConnection ({{mgNode->nodeID,     ch}, {outNode->nodeID, ch}});
        }

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

        kbProcessor        = nullptr;
        meterGainProcessor = nullptr;
        getOrCreateSlot().detach();
        audioGraph.clear();

        auto outNode = audioGraph.addNode (
            std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (
                AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

        auto* mgProc = new GainAndMeterProcessor();
        mgProc->setGain (pendingGain);
        meterGainProcessor = mgProc;
        auto mgNode = audioGraph.addNode (std::unique_ptr<GainAndMeterProcessor> (mgProc));

        for (int ch = 0; ch < 2; ++ch)
            audioGraph.addConnection ({{mgNode->nodeID, ch}, {outNode->nodeID, ch}});

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

        for (auto* b : masterEffects)
        {
            auto effectNode = audioGraph.addNode (
                std::make_unique<BridgeEffectProcessor> (
                    b->getSharedMemory(), b->getSyncEvents(), b));
            if (lastNode != nullptr)
                for (int ch = 0; ch < 2; ++ch)
                    audioGraph.addConnection ({{lastNode->nodeID, ch}, {effectNode->nodeID, ch}});
            lastNode = effectNode;
        }

        if (lastNode != nullptr)
            for (int ch = 0; ch < 2; ++ch)
                audioGraph.addConnection ({{lastNode->nodeID, ch}, {mgNode->nodeID, ch}});

        if (lastSampleRate > 0.0 && lastBufferSize > 0)
            audioGraph.prepareToPlay (lastSampleRate, lastBufferSize);
    }

    void buildGraphWithSineWave()
    {
        kbProcessor       = nullptr;
        meterGainProcessor = nullptr;
        getOrCreateSlot().detach();
        audioGraph.clear();

        auto outNode  = audioGraph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
        auto midiNode = audioGraph.addNode (std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor> (AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));
        auto sineNode = audioGraph.addNode (std::make_unique<SineWaveProcessor> (keyboardState));

        // Gain + meter node between sine and output
        auto* mgProc  = new GainAndMeterProcessor();
        mgProc->setGain (pendingGain);
        meterGainProcessor = mgProc;
        auto mgNode   = audioGraph.addNode (std::unique_ptr<GainAndMeterProcessor> (mgProc));

        for (int ch = 0; ch < 2; ++ch)
        {
            audioGraph.addConnection ({{sineNode->nodeID, ch}, {mgNode->nodeID, ch}});
            audioGraph.addConnection ({{mgNode->nodeID,   ch}, {outNode->nodeID, ch}});
        }
        audioGraph.addConnection ({{midiNode->nodeID, AudioProcessorGraph::midiChannelIndex},
                                   {sineNode->nodeID, AudioProcessorGraph::midiChannelIndex}});

        // Prepare all new nodes so they have a valid sample rate from the start.
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

                kbProcessor        = nullptr;
                meterGainProcessor = nullptr;
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

                // Gain + meter node between plugin and output
                auto* mgProc  = new GainAndMeterProcessor();
                mgProc->setGain (pendingGain);
                meterGainProcessor = mgProc;
                auto mgNode   = audioGraph.addNode (std::unique_ptr<GainAndMeterProcessor> (mgProc));

                for (int ch = 0; ch < 2; ++ch)
                {
                    audioGraph.addConnection ({{plugNode->nodeID, ch}, {mgNode->nodeID, ch}});
                    audioGraph.addConnection ({{mgNode->nodeID,   ch}, {outNode->nodeID, ch}});
                }

                audioGraph.addConnection ({{midiNode->nodeID, AudioProcessorGraph::midiChannelIndex},
                                           {plugNode->nodeID, AudioProcessorGraph::midiChannelIndex}});
                audioGraph.addConnection ({{kbNode->nodeID,   AudioProcessorGraph::midiChannelIndex},
                                           {plugNode->nodeID, AudioProcessorGraph::midiChannelIndex}});

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

private:
    PluginSlot& getOrCreateSlot (int index = 0)
    {
        while (slots.size() <= index)
            slots.add (new PluginSlot());
        return *slots[index];
    }

    MidiKeyboardState& keyboardState;
    AudioProcessorGraph audioGraph;
    AudioProcessorPlayer audioProcessorPlayer;
    AudioPluginFormatManager formatManager;
    OwnedArray<PluginSlot> slots;
    MidiInjectionsProcessor*  kbProcessor        = nullptr; // raw ptr; owned by audioGraph
    GainAndMeterProcessor*    meterGainProcessor  = nullptr; // raw ptr; owned by audioGraph
    float  pendingGain    = 1.0f; // gain to apply when graph is next rebuilt
    int    pendingTranspose = 0;
    int    pendingChannel   = 0;
    double lastSampleRate   = 0.0; // cached from last initialise() / loadPlugin() call
    int    lastBufferSize   = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
