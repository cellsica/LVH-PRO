#pragma once

#include <JuceHeader.h>
#include "../ProcessorSDK/IProcessorPlugin.h"
#include <array>
#include <atomic>

/**
 * @class ProcessorPluginNode
 * @brief JUCE AudioProcessor wrapper for an IProcessorPlugin instance.
 *
 * Inserted as a stereo in/out node in the AudioProcessorGraph by
 * AudioEngine::rebuildBridgeGraph().  Bridges the JUCE audio graph API
 * (AudioBuffer<float>) to the flat C-array API used by IProcessorPlugin.
 *
 * The wrapped IProcessorPlugin is **not** owned by this class; the
 * ProcessorManager retains ownership and manages the plugin lifecycle.
 *
 * **Thread safety:**
 * - processBlock() is called from the audio thread.
 * - exchangePeak() is called from the message thread.
 * - Both use std::atomic with relaxed ordering (peak values are
 *   display-only; strict ordering is not required).
 */
class ProcessorPluginNode : public juce::AudioProcessor
{
public:
    explicit ProcessorPluginNode (IProcessorPlugin* plugin)
        : juce::AudioProcessor (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
        , plugin_ (plugin)
    {
        peaks_[0].store (0.f);
        peaks_[1].store (0.f);
    }

    // ── AudioProcessor ────────────────────────────────────────────────────────

    void prepareToPlay (double sampleRate, int maxBufferSize) override
    {
        peaks_[0].store (0.f);
        peaks_[1].store (0.f);
        tempOutput_.setSize (2, maxBufferSize, false, true, false);

        // Re-initialise the plugin when the device configuration changes.
        if (plugin_ != nullptr)
            plugin_->initialise (sampleRate, maxBufferSize);
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (plugin_ == nullptr) return;

        const int numChannels = juce::jmin (2, buffer.getNumChannels());
        const int numSamples  = buffer.getNumSamples();

        // Build C-array pointer views required by IProcessorPlugin.
        inPtrs_[0]  = buffer.getReadPointer (0);
        inPtrs_[1]  = (numChannels > 1) ? buffer.getReadPointer (1)
                                         : buffer.getReadPointer (0);
        outPtrs_[0] = tempOutput_.getWritePointer (0);
        outPtrs_[1] = tempOutput_.getWritePointer (1);

        plugin_->processBlock (inPtrs_.data(), outPtrs_.data(),
                               numChannels, numSamples);

        // Write output back into buffer.
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom (ch, 0, tempOutput_, ch, 0, numSamples);

        // Update peak atomics (audio thread → message thread).
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float p   = buffer.getMagnitude (ch, 0, numSamples);
            const float cur = peaks_[ch].load (std::memory_order_relaxed);
            if (p > cur)
                peaks_[ch].store (p, std::memory_order_relaxed);
        }
    }

    // ── Peak metering (message thread) ────────────────────────────────────────

    /** @brief Read and atomically reset the output peak for channel @p ch.
     *  @param ch  0 = Left, 1 = Right.
     *  @return    Peak magnitude since the last call. */
    float exchangePeak (int ch) noexcept
    {
        return peaks_[ch].exchange (0.f, std::memory_order_relaxed);
    }

    // ── AudioProcessor boilerplate ────────────────────────────────────────────

    const juce::String getName() const override
    {
        return plugin_ != nullptr ? juce::String (plugin_->getName())
                                  : juce::String ("ProcessorPlugin");
    }

    bool  acceptsMidi()  const override { return false; }
    bool  producesMidi() const override { return false; }
    bool  isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    int  getNumPrograms() override                          { return 1; }
    int  getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override  {}
    void setStateInformation (const void*, int) override    {}

private:
    IProcessorPlugin*  plugin_ = nullptr;

    juce::AudioBuffer<float> tempOutput_;

    // Pointer arrays reused every processBlock() to avoid heap allocation.
    std::array<const float*, 2> inPtrs_  {};
    std::array<float*, 2>       outPtrs_ {};

    std::array<std::atomic<float>, 2> peaks_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorPluginNode)
};
