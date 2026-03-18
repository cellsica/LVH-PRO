#pragma once
#include <JuceHeader.h>
#include "IPCManager.h"

// =====================================================================
// BridgeSyncProcessor
//
// Inserted into Core's AudioProcessorGraph as the "plugin" node when
// Bridge mode is active. Instead of processing audio locally, it:
//   1. Copies Core's audio input into SharedAudioLayout::audioIn
//   2. Signals Bridge to process via SyncEvents::signalRequest()
//   3. Waits for Bridge to finish via SyncEvents::waitForDone()
//   4. Copies Bridge's output from SharedAudioLayout::audioOut
//
// NOTE: processBlock() briefly blocks the audio thread while waiting
// for Bridge (up to kProcessTimeoutMs). This is acceptable for a
// first-pass implementation; future work can make this asynchronous.
// =====================================================================
class BridgeSyncProcessor : public juce::AudioProcessor
{
public:
    // In Debug builds, plugin processBlock is much slower due to debug overhead.
    // Use a generous timeout so waitForDone() doesn't falsely expire → silence.
   #if JUCE_DEBUG
    static constexpr int kProcessTimeoutMs = 200;
   #else
    static constexpr int kProcessTimeoutMs = 10;
   #endif

    BridgeSyncProcessor (SharedMemoryBuffer& shm, SyncEvents& events)
        : juce::AudioProcessor (BusesProperties()
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
        , shm (shm), events (events)
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
        auto* layout = shm.getLayout();
        if (layout == nullptr || ! events.isOpen())
        {
            buffer.clear();
            return;
        }

        const int numSamples  = buffer.getNumSamples();
        const int numChannels = juce::jmin (2, buffer.getNumChannels());

        // 1. Copy Core's input audio into shared memory
        for (int ch = 0; ch < numChannels; ++ch)
            std::memcpy (layout->audioIn[ch],
                         buffer.getReadPointer (ch),
                         (size_t)numSamples * sizeof (float));

        // 2. Signal Bridge to start processBlock
        events.signalRequest();

        // 3. Wait for Bridge to finish
        if (events.waitForDone (kProcessTimeoutMs))
        {
            // 4. Copy Bridge's processed output
            for (int ch = 0; ch < numChannels; ++ch)
                std::memcpy (buffer.getWritePointer (ch),
                             layout->audioOut[ch],
                             (size_t)numSamples * sizeof (float));
        }
        else
        {
            // Timeout: Bridge too slow or not running — output silence
            buffer.clear();
        }
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::disabled();
    }

    // AudioProcessor boilerplate
    const juce::String getName() const override                { return "BridgeSyncProcessor"; }
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeSyncProcessor)
};

// =====================================================================
// MultiSourceBridgeProcessor
//
// Replaces multiple BridgeSyncProcessor nodes with a single node that:
//   1. Signals ALL bridges simultaneously (parallel processing)
//   2. Waits for each bridge and SUMS their outputs into one buffer
//
// This avoids sequential blocking (which would exceed the audio callback
// budget with N bridges) and eliminates any JUCE graph summing ambiguity.
// =====================================================================
class MultiSourceBridgeProcessor : public juce::AudioProcessor
{
public:
   #if JUCE_DEBUG
    static constexpr int kProcessTimeoutMs = 200;
   #else
    static constexpr int kProcessTimeoutMs = 15;
   #endif

    struct BridgeSource
    {
        SharedMemoryBuffer* shm;
        SyncEvents*         events;
    };

    explicit MultiSourceBridgeProcessor (std::vector<BridgeSource> sources)
        : juce::AudioProcessor (BusesProperties()
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
        , sources_ (std::move (sources))
    {}

    void prepareToPlay (double sampleRate, int maxBlockSize) override
    {
        for (auto& src : sources_)
            if (auto* layout = src.shm->getLayout())
            {
                layout->sampleRate  = static_cast<float> (sampleRate);
                layout->bufferSize  = maxBlockSize;
                layout->numChannels = 2;
            }
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();
        const int numSamples  = juce::jmin (buffer.getNumSamples(), 4096);
        const int numChannels = juce::jmin (2, buffer.getNumChannels());

        // Step 1: Signal ALL bridges simultaneously.
        for (auto& src : sources_)
            if (src.events->isOpen())
                src.events->signalRequest();

        // Step 2: Wait for each bridge and ADD its output to the buffer.
        for (auto& src : sources_)
        {
            if (! src.events->isOpen()) continue;
            if (src.events->waitForDone (kProcessTimeoutMs))
            {
                auto* layout = src.shm->getLayout();
                if (layout != nullptr)
                    for (int ch = 0; ch < numChannels; ++ch)
                        buffer.addFrom (ch, 0, layout->audioOut[ch], numSamples);
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
    std::vector<BridgeSource> sources_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiSourceBridgeProcessor)
};
