#pragma once

/**
 * @file ProcessorPluginNode.h
 * @brief JUCE AudioProcessor wrapper that adapts an IProcessorPlugin for use
 *        in the AudioProcessorGraph.
 */

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
    /**
     * @brief Constructs the node with a non-owning pointer to the plugin.
     * @param plugin  IProcessorPlugin instance managed by ProcessorManager.
     *                Must remain valid for the lifetime of this node.
     */
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

    /**
     * @brief Allocates internal buffers and calls IProcessorPlugin::initialise().
     *
     * Called by the AudioProcessorGraph whenever the audio device configuration
     * changes (sample rate, buffer size).  Resets peak atomics.
     *
     * @param sampleRate    Current device sample rate in Hz.
     * @param maxBufferSize Maximum buffer size that will be passed to processBlock().
     */
    void prepareToPlay (double sampleRate, int maxBufferSize) override
    {
        peaks_[0].store (0.f);
        peaks_[1].store (0.f);
        tempOutput_.setSize (2, maxBufferSize, false, true, false);

        // Re-initialise the plugin when the device configuration changes.
        if (plugin_ != nullptr)
            plugin_->initialise (sampleRate, maxBufferSize);
    }

    /** @brief No-op — resources are released in IProcessorPlugin::shutdown(). */
    void releaseResources() override {}

    /**
     * @brief Routes one audio block through the IProcessorPlugin, then applies
     *        mixer gain/mute and updates peak meters.
     *
     * Called from the audio thread.  Steps performed in order:
     * 1. Build `const float**` / `float**` pointer views of @p buffer.
     * 2. Call IProcessorPlugin::processBlock() with those views.
     * 3. Copy the plugin output back into @p buffer.
     * 4. Apply mixerGain / mixerMuted (atomic reads; no blocking).
     * 5. Update peaks_ atomics for the message-thread meter display.
     *
     * @param buffer   Stereo input/output buffer.  Contents are replaced on return.
     */
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

        // Apply mixer gain / mute (audio thread — atomic reads).
        {
            const float g = mixerMuted.load (std::memory_order_relaxed)
                                ? 0.f
                                : mixerGain.load (std::memory_order_relaxed);
            if (g != 1.0f)
                buffer.applyGain (g);
        }

        // Update peak atomics (audio thread → message thread).
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float p   = buffer.getMagnitude (ch, 0, numSamples);
            const float cur = peaks_[ch].load (std::memory_order_relaxed);
            if (p > cur)
                peaks_[ch].store (p, std::memory_order_relaxed);
        }
    }

    // ── Mixer control (set from message thread, read on audio thread) ─────────

    /** @brief Linear output gain applied after processBlock().  Range [0.0, 1.5].
     *         Written on the message thread by AudioEngine::setProcessorGain(). */
    std::atomic<float> mixerGain  { 1.0f };

    /** @brief When true the output is silenced regardless of mixerGain.
     *         Written on the message thread by AudioEngine::setProcessorMuted(). */
    std::atomic<bool>  mixerMuted { false };

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
