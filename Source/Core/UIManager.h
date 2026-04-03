#pragma once
#include <JuceHeader.h>
#include "AudioEngine.h"
#include "BridgeManager.h"
#include "ProjectSerializer.h"
#include "MidiRoutingManager.h"
#include "StageManager.h"
#include "MixerWindow.h"
#include "../MetronomeWindow.h"
#include "../VUMeterWindow.h"
#include "VisualizerManager.h"
#include "../VisualizerWindow.h"

// Forward declarations — full definitions only needed in UIManager.cpp
class MainComponent;
class SettingsWindow;
class StageWindow;
class PluginPickerComponent;
class ProcessorManager;
class ProcessorDispatcherWindow;

/**
 * @class UIManager
 * @brief Owns and coordinates all floating windows and UI state.
 *
 * Responsibilities:
 * - Lifecycle management for MixerWindow, SettingsWindow, StageWindow,
 *   MetronomeWindow, VUMeterWindow, and VisualizerWindow.
 * - Master volume state (owned here; queried by ProjectSerializer via callbacks).
 * - Plugin favourites list (persisted in ApplicationProperties).
 * - Mixer MIDI Learn / CC mapping.
 * - Logo right-click main menu.
 * - Bridge file chooser dialogs.
 *
 * **Construction order:**
 * 1. Construct UIManager (injects all dependencies).
 * 2. Create the main window and MainComponent.
 * 3. Call setMainComponent() to wire volume/menu/launch callbacks.
 * 4. Call restoreStageWindow() to show the StageWindow if it was open last session.
 *
 * **Shutdown order:**
 * Call shutdown() *before* destroying the main window, so that floating
 * windows can be closed safely while the message loop is still running.
 *
 * **Thread safety:** All public methods must be called from the message thread.
 */
class UIManager
{
public:
    /**
     * @brief Constructs a UIManager.
     * @param audioEngine       The application audio engine.
     * @param bridgeManager     Bridge lifecycle manager.
     * @param projectSerializer Project file I/O handler.
     * @param midiRouter        MIDI routing and transpose manager.
     * @param deviceManager     JUCE audio device manager.
     * @param appProperties     Persistent application properties store.
     * @param knownPlugins      JUCE plugin list (for the plugin picker).
     * @param stageManager      Stage Set lifecycle manager.
     *
     * All references must outlive this object.
     */
    UIManager (AudioEngine&                 audioEngine,
               BridgeManager&               bridgeManager,
               ProjectSerializer&           projectSerializer,
               MidiRoutingManager&          midiRouter,
               juce::AudioDeviceManager&    deviceManager,
               juce::ApplicationProperties& appProperties,
               juce::KnownPluginList&       knownPlugins,
               StageManager&                stageManager);

    ~UIManager();

    // ── Callbacks — wired by LvhProApplication ────────────────────────────────

    /** @brief Fired when the user requests a plugin scan via the main menu. */
    std::function<void()> onStartPluginScan;

    /** @brief Fired when the user changes the color theme (0=Dark, 1=Light). */
    std::function<void(int)> onThemeChanged;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /**
     * @brief Complete post-construction wiring once the main window exists.
     *
     * Wires volume slider, logo menu, and bridge-launch callbacks on MainComponent.
     * Must be called after the main window and MainComponent are created.
     *
     * @param mc  Pointer to the application's MainComponent.  Must not be nullptr.
     */
    void setMainComponent (MainComponent* mc);

    /**
     * @brief Prepare for application shutdown.
     *
     * Saves window state to ApplicationProperties and closes all floating windows.
     * Must be called at the start of LvhProApplication::shutdown(), before the
     * main window is destroyed.
     */
    void shutdown();

    // ── Window management ─────────────────────────────────────────────────────

    /**
     * @brief Show or hide the Mixer Console window.
     * @param show  true to show, false to hide.
     */
    void toggleMixerWindow (bool show);

    /**
     * @brief Show or hide the Stage Set window.
     * @param show  true to show, false to hide.
     */
    void toggleStageWindow (bool show);

    /**
     * @brief Show or hide the Metronome window.
     * @param show  true to show, false to hide.
     */
    void toggleMetronomeWindow (bool show);

    /**
     * @brief Show or hide the VU Meter window.
     * @param show  true to show, false to hide.
     */
    void toggleVuMeterWindow (bool show);

    /**
     * @brief Show or hide the Visualizer window.
     *
     * On first show, scans the Visualizers/ directory for DLLs and restores
     * the last-used plugin, auto-switch mode, and opacity from ApplicationProperties.
     *
     * @param show  true to show, false to hide.
     */
    void toggleVisualizerWindow (bool show);

    /** @brief Show or hide the Processor Manager dispatcher window. */
    void toggleProcessorDispatcher (bool show);

    /** @brief Open the Settings window (audio device, language, theme). */
    void openSettings();

    /**
     * @brief Restore the StageWindow visibility and bounds from ApplicationProperties.
     *
     * Call once during application initialise, after all callbacks are wired.
     */
    void restoreStageWindow();

    // ── Mixer bridge list ─────────────────────────────────────────────────────

    /**
     * @brief Update the Mixer Console with a new bridge layout.
     *
     * Called from BridgeManager::onGraphRebuilt after the audio graph changes.
     *
     * @param instruments  Bridges with the Instrument role.
     * @param effects      Bridges with the Effect (master chain) role.
     */
    void updateMixerBridges (juce::Array<BridgeInstance*> instruments,
                             juce::Array<BridgeInstance*> effects);

    // ── Master volume ─────────────────────────────────────────────────────────

    /**
     * @brief Returns the current master output volume.
     * @return Linear gain value (1.0 = unity, 0.0 = silence).
     */
    double getMasterVolume() const noexcept { return masterVolume_; }

    /**
     * @brief Set the master output volume and synchronise all UI elements.
     *
     * Updates the AudioEngine gain, the toolbar volume slider, and the Mixer
     * Console master fader simultaneously.
     *
     * @param vol  Linear gain value [0.0, 1.0].
     */
    void setMasterVolume (double vol);

    // ── Mixer window state ────────────────────────────────────────────────────

    /**
     * @brief Returns whether the Mixer Console window is currently visible.
     * @return true if the window exists and is visible.
     */
    bool isMixerWindowVisible() const noexcept;

    /**
     * @brief Returns the current screen bounds of the Mixer Console window.
     * @return Rectangle in screen coordinates, or an empty rectangle if hidden.
     */
    juce::Rectangle<int> getMixerWindowBounds() const noexcept;

    /**
     * @brief Restore the Mixer Console visibility and position from a project load.
     * @param visible  true to show the window.
     * @param bounds   Screen position and size to apply.
     */
    void restoreMixerWindow (bool visible, juce::Rectangle<int> bounds);

    /**
     * @brief Synchronise the MetronomeWindow UI from the current AudioEngine state.
     *
     * Call this after loadProject() completes so that tempo, time signature,
     * and click volume reflect the restored values.
     */
    void syncMetronomeWindowFromEngine();

    /**
     * @brief Refresh all open windows after a language change.
     *
     * Re-creates window content so that all localised strings are updated.
     */
    void refreshAllWindows();

    // ── Plugin favourites ─────────────────────────────────────────────────────

    /**
     * @brief Toggle the favourite state of a plugin.
     * @param pluginId  The plugin's unique identifier string.
     */
    void toggleFavorite (const juce::String& pluginId);

    /**
     * @brief Returns whether a plugin is marked as a favourite.
     * @param pluginId  The plugin's unique identifier string.
     * @return true if the plugin is in the favourites list.
     */
    bool isFavorite (const juce::String& pluginId) const;

    /**
     * @brief Returns the list of all favourite plugin identifiers.
     * @return StringArray of plugin ID strings.
     */
    const juce::StringArray& getFavoriteIds() const noexcept { return favoriteIds_; }

    // ── Plugin disabled (hidden) list ─────────────────────────────────────────

    /**
     * @brief Toggle the disabled (hidden) state of a plugin.
     * @param pluginId  The plugin's unique identifier string.
     */
    void toggleDisabled (const juce::String& pluginId);

    /**
     * @brief Returns whether a plugin is disabled (hidden from the picker).
     * @param pluginId  The plugin's unique identifier string.
     * @return true if the plugin is in the disabled list.
     */
    bool isDisabled (const juce::String& pluginId) const;

    /**
     * @brief Returns the list of all disabled plugin identifiers.
     * @return StringArray of plugin ID strings.
     */
    const juce::StringArray& getDisabledIds() const noexcept { return disabledIds_; }

    // ── Processor plugin dispatch (Mission 053) ───────────────────────────────

    /** @brief Number of processor DLLs found in the Processors/ directory. */
    int getNumDiscoveredProcessors() const;

    /**
     * @brief Display name of the discovered processor at @p index.
     *
     * Returns the name from IProcessorPlugin::getName() when running,
     * otherwise the DLL filename stem.
     */
    juce::String getProcessorName (int index) const;

    /** @brief Returns whether the processor at @p index is currently running. */
    bool isProcessorRunning (int index) const;

    /**
     * @brief Accent colour of the processor at @p index.
     *
     * Returns the plugin's reported colour when running, or the default
     * (0xff556688) when stopped.
     */
    unsigned int getProcessorAccentColour (int index) const;

    /**
     * @brief Start the processor at @p index.
     *
     * Loads the DLL, creates the instance, wires it into the audio graph,
     * and adds a mixer strip.  No-op if already running.
     */
    void startProcessor (int index);

    /**
     * @brief Stop the processor at @p index.
     *
     * Shuts down the instance, removes it from the audio graph, and removes
     * the mixer strip.  No-op if not running.
     */
    void stopProcessor (int index);

    /**
     * @brief Process an incoming MIDI message for remote control.
     *
     * Handles master volume CC, stage-slot selection, and mixer channel CC
     * (based on the MIDI Learn mappings).  Must be called from the message thread.
     *
     * @param msg  The MIDI message to process.
     */
    void handleMidiRemote (const juce::MidiMessage& msg);

    // ── Bridge file choosers ───────────────────────────────────────────────────

    /**
     * @brief Open a native file chooser and launch a bridge for the selected plugin.
     * @param role  The role (Instrument or Effect) to assign to the launched bridge.
     */
    void launchBridgeFileChooser (BridgeInstance::Role role = BridgeInstance::Role::Instrument);

private:
    void showMainMenu();
    void showPluginPicker (BridgeInstance::Role fixedRole, BridgeInstance* parentInstrument = nullptr);
    void showPluginPicker (BridgeInstance::Role fixedRole, const juce::String& parentPath);
    /** @brief Rebuild MixerWindow processor strips and (re)wire their callbacks.
     *
     *  Reads the current plugin list from processorManager_, builds
     *  ProcessorStripInfo entries, and calls MixerWindow::updateProcessorStrips().
     *  Also wires getProcessorPeaks / onProcessorGainChange / onProcessorMuteChange
     *  on the window to the corresponding AudioEngine methods.
     *
     *  Safe to call when mixerWindow_ is nullptr (no-op).
     */
    void updateMixerProcessorStrips();

    // ── Processor persistence helpers ─────────────────────────────────────────
    /** @brief Persist the current set of running processor names to ApplicationProperties. */
    void saveActiveProcessors();
    /** @brief Restore previously running processors after startup scanOnly(). */
    void restoreActiveProcessors();
    /**
     * @brief Returns the index into AudioEngine's active-instance list for
     *        discovered processor @p discoveredIndex.
     *
     * Counts how many processors before @p discoveredIndex are currently running.
     * Returns -1 if the processor at @p discoveredIndex is not running.
     */
    int getActiveIndexOf (int discoveredIndex) const;
    juce::File getBridgeStartDir() const;
    void executeSafeSetOperation (std::function<void()> action);

    // ── Mixer MIDI mapping ────────────────────────────────────────────────────
    struct MixerMidiMapping
    {
        int ccFader = -1;  ///< CC number for fader (-1 = not mapped).
        int ccPan   = -1;  ///< CC number for pan.
        int ccMute  = -1;  ///< CC number for mute toggle.
        int ccSolo  = -1;  ///< CC number for solo toggle.
    };

    struct LearnState
    {
        BridgeInstance* bridge = nullptr;
        MixerParam      param  = MixerParam::Fader;
        bool            active = false;
    };

    void startMidiLearn   (BridgeInstance* b, MixerParam p);
    void clearMidiMapping (BridgeInstance* b, MixerParam p);
    void saveMixerMappings();
    void loadMixerMappings();
    BridgeInstance* findBridgeByPath (const juce::String& path) const;

    // ── Constructor-injected references ───────────────────────────────────────
    AudioEngine&                 audioEngine_;
    BridgeManager&               bridgeManager_;
    ProjectSerializer&           projectSerializer_;
    MidiRoutingManager&          midiRouter_;
    juce::AudioDeviceManager&    deviceManager_;
    juce::ApplicationProperties& appProperties_;
    juce::KnownPluginList&       knownPlugins_;
    StageManager&                stageManager_;

    // ── State ─────────────────────────────────────────────────────────────────
    MainComponent*                        mc_             = nullptr;
    juce::StringArray                     favoriteIds_;
    juce::StringArray                     disabledIds_;
    double                                masterVolume_   = 1.0;

    std::unique_ptr<MixerWindow>          mixerWindow_;
    std::unique_ptr<MetronomeWindow>      metronomeWindow_;
    std::unique_ptr<SettingsWindow>       settingsWindow_;
    std::unique_ptr<StageWindow>          stageWindow_;
    std::unique_ptr<VUPhysicsEngine>      vuPhysicsEngine_;
    std::unique_ptr<VUMeterWindow>        vuMeterWindow_;
    std::unique_ptr<VisualizerManager>    visualizerManager_;
    std::unique_ptr<VisualizerWindow>           visualizerWindow_;
    std::unique_ptr<ProcessorDispatcherWindow>  processorDispatcherWindow_;
    std::unique_ptr<juce::DocumentWindow>       pluginPickerWindow_;
    std::unique_ptr<ProcessorManager>           processorManager_;

    LearnState                               learnState_;
    std::map<juce::String, MixerMidiMapping> mixerMappings_;

    void updateMidiDeviceLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UIManager)
};
