#pragma once

#include <JuceHeader.h>
#include "../VisualizerSDK/IVisualizerPlugin.h"
#include "../VisualizerSDK/IAudioSource.h"

// Forward declaration to avoid circular include with AudioEngine.h
class AudioEngine;

// =============================================================================
// VisualizerManager
//
// Responsibilities:
//   1. Scan <exe dir>/Visualizers/ for *.dll files.
//   2. Load each DLL with juce::DynamicLibrary, resolve createVisualizer().
//   3. Manage plugin lifecycle (initialise / render / shutdown).
//   4. Implement IAudioSource so plugins can query FFT / waveform data.
//
// Thread safety:
//   All public methods (except render) must be called from the message thread.
//   render() is also message-thread only (called from a repaint/timer).
//   The IAudioSource methods are likewise called from the plugin's render(),
//   which runs on the message thread.  Audio data is copied with a SpinLock.
// =============================================================================
class VisualizerManager final : public IAudioSource
{
public:
    VisualizerManager();
    ~VisualizerManager() override;

    // ------------------------------------------------------------------
    // Setup — call before scanAndLoad()
    // ------------------------------------------------------------------
    void setAudioEngine    (AudioEngine* engine)  noexcept { audioEngine_     = engine; }
    void setCurrentBPM     (double bpm)            noexcept { currentBpm_      = bpm;    }
    void setCurrentSampleRate (double sr)          noexcept { currentSampleRate_ = sr;   }

    // ------------------------------------------------------------------
    // Plugin management (message thread)
    // ------------------------------------------------------------------

    /** Scan `vizDirectory` for *.dll files and load each one.
        Any previously loaded plugins are unloaded first. */
    void scanAndLoad (const juce::File& vizDirectory);

    /** Unload all currently loaded plugins. */
    void unloadAll();

    /** Number of successfully loaded plugins. */
    int getNumPlugins() const noexcept { return (int)plugins_.size(); }

    // ------------------------------------------------------------------
    // Rendering (message thread)
    // ------------------------------------------------------------------

    /** Call this from a repaint / timer callback to forward the render
        call to every loaded plugin.  openGLContext may be nullptr. */
    void render (juce::Graphics& g, juce::OpenGLContext* openGLContext);

    // ------------------------------------------------------------------
    // IAudioSource implementation
    // ------------------------------------------------------------------
    void   getFFTData      (float* buffer, int size) override;
    void   getWaveformData (float* buffer, int size) override;
    double getSampleRate() const override { return currentSampleRate_; }
    double getBPM()        const override { return currentBpm_;        }

private:
    // Each successfully loaded DLL and its plugin instance.
    struct LoadedPlugin
    {
        std::unique_ptr<juce::DynamicLibrary> library;
        IVisualizerPlugin*                    instance = nullptr;
        juce::String                          name;

        ~LoadedPlugin()
        {
            if (instance != nullptr)
            {
                instance->shutdown();
                delete instance;
                instance = nullptr;
            }
        }
    };

    AudioEngine* audioEngine_     = nullptr;
    double       currentSampleRate_ = 44100.0;
    double       currentBpm_        = 120.0;

    std::vector<std::unique_ptr<LoadedPlugin>> plugins_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizerManager)
};
