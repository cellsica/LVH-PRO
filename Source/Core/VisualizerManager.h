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
//   5. (Phase D) Manage which plugin is currently displayed.
//   6. (Phase D) Auto-switch plugins on a timer (Sequential / Random).
//   7. (Phase E) Rescan Visualizers/ folder at runtime.
//
// Thread safety:
//   All public methods (except render) must be called from the message thread.
//   render() is also message-thread only (called from a repaint/timer).
//   The IAudioSource methods are likewise called from the plugin's render(),
//   which runs on the message thread.  Audio data is copied with a SpinLock.
// =============================================================================
class VisualizerManager final : public IAudioSource,
                                private juce::Timer
{
public:
    // ------------------------------------------------------------------
    // Auto-switching playback mode
    // ------------------------------------------------------------------
    enum class SwitchMode
    {
        Manual,      // User selects manually — no auto-switch timer
        Sequential,  // Advance through plugins in order at switchIntervalSec_
        Random       // Pick a random plugin (different from current) at switchIntervalSec_
    };

    VisualizerManager();
    ~VisualizerManager() override;

    // ------------------------------------------------------------------
    // Setup — call before scanAndLoad()
    // ------------------------------------------------------------------
    void setAudioEngine       (AudioEngine* engine) noexcept { audioEngine_      = engine; }
    void setCurrentBPM        (double bpm)           noexcept { currentBpm_       = bpm;    }
    void setCurrentSampleRate (double sr)            noexcept { currentSampleRate_ = sr;    }

    // ------------------------------------------------------------------
    // Plugin management (message thread)
    // ------------------------------------------------------------------

    /** Scan `vizDirectory` for *.dll files and load each one.
        Any previously loaded plugins are unloaded first. */
    void scanAndLoad (const juce::File& vizDirectory);

    /** Re-scan the previously used directory and reload all plugins.
        Tries to restore the previously selected plugin index. */
    void rescan();

    /** Unload all currently loaded plugins. */
    void unloadAll();

    /** Number of successfully loaded plugins. */
    int getNumPlugins() const noexcept { return (int)plugins_.size(); }

    /** Returns the names of all loaded plugins (DLL filename without extension). */
    juce::StringArray getPluginNames() const;

    /** ロード済み現在プラグインの推奨サイズを返す。
        プラグイン未ロード時はデフォルト値 (500x500) を返す。 */
    juce::Rectangle<int> getPreferredSize() const noexcept
    {
        if (plugins_.empty()) return { 0, 0, 500, 500 };

        int idx = juce::jlimit (0, (int)plugins_.size() - 1, currentPluginIndex_);
        if (plugins_[idx]->instance != nullptr)
            return { 0, 0,
                     plugins_[idx]->instance->getPreferredWidth(),
                     plugins_[idx]->instance->getPreferredHeight() };
        return { 0, 0, 500, 500 };
    }

    // ------------------------------------------------------------------
    // Plugin selection (message thread)
    // ------------------------------------------------------------------

    /** Returns the index of the currently displayed plugin. */
    int getCurrentPluginIndex() const noexcept { return currentPluginIndex_; }

    /** Switch to the plugin at `index` (clamped to valid range). */
    void setCurrentPlugin (int index);

    /** Advance to the next plugin according to the current SwitchMode. */
    void nextPlugin();

    // ------------------------------------------------------------------
    // Auto-switching control (message thread)
    // ------------------------------------------------------------------

    SwitchMode getSwitchMode()     const noexcept { return switchMode_; }
    int        getSwitchInterval() const noexcept { return switchIntervalSec_; }

    /** Set the playback mode.  Starts or stops the internal timer as needed. */
    void setSwitchMode (SwitchMode mode);

    /** Set the auto-switch interval in seconds.  Restarts timer if active. */
    void setSwitchInterval (int seconds);

    // ------------------------------------------------------------------
    // State-change callback (Phase E)
    // ------------------------------------------------------------------

    /** Fired on the message thread whenever currentPluginIndex_, switchMode_,
        or switchIntervalSec_ changes due to user action.
        UIManager uses this to persist settings to ApplicationProperties. */
    std::function<void()> onStateChanged;

    // ------------------------------------------------------------------
    // Rendering (message thread)
    // ------------------------------------------------------------------

    /** Call this from a repaint / timer callback.
        Renders only the currently selected plugin.  openGLContext may be nullptr. */
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

    // juce::Timer — fires when it's time to switch to the next plugin
    void timerCallback() override;

    AudioEngine* audioEngine_      = nullptr;
    double       currentSampleRate_ = 44100.0;
    double       currentBpm_        = 120.0;

    std::vector<std::unique_ptr<LoadedPlugin>> plugins_;

    // Phase D state
    int        currentPluginIndex_ = 0;
    SwitchMode switchMode_         = SwitchMode::Manual;
    int        switchIntervalSec_  = 20;
    juce::Random random_;

    // Phase E state
    juce::File vizDirectory_;           // stored by scanAndLoad for rescan()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizerManager)
};
