#pragma once
#include <JuceHeader.h>
#include <optional>
#include "BridgeInstance.h"
#include "MidiRoutingManager.h"
#include "AudioEngine.h"

/**
 * @class ProjectSerializer
 * @brief Handles saving and loading of `.lvh` project files.
 *
 * Owns all project file I/O extracted from LvhProApplication:
 * - saveProject() / loadProject() / writeProjectXml()
 * - Pending state maps (plugin states, mixer settings, window bounds)
 *   consumed via the closure-injection pattern at launch time.
 * - The currently open project file path.
 *
 * **Dependencies** injected via constructor (all must outlive this object):
 * bridges, audioEngine, midiRouter, deviceManager, appProperties.
 *
 * **Closure injection pattern:**
 * When a project is loaded, plugin data is stored in internal pending maps.
 * BridgeManager calls takePending*() just before launching each bridge, and
 * captures the returned data in the onConnected lambda.  This means
 * onConnected never calls back into ProjectSerializer.
 *
 * **loadProject() modes:**
 * | isGlobal | globalLayerSwitch | Behaviour |
 * |----------|-------------------|-----------|
 * | true     | false             | Slot 0: full reset, all bridges marked Global |
 * | false    | true              | Slot 1+: instrument-only switch, Global layer preserved |
 * | false    | false             | Direct open: full reset, no Global marking |
 */
class ProjectSerializer
{
public:
    // ── Nested types ──────────────────────────────────────────────────────────

    /**
     * @brief Per-channel mixer settings stored in a project file.
     */
    struct MixerSettings
    {
        float        gain     = 1.f;    ///< Channel gain (linear, 1.0 = 0 dB).
        float        pan      = 0.f;    ///< Pan position [-1.0, +1.0].
        bool         muted    = false;  ///< Channel mute state.
        bool         bypassed = false;  ///< Plugin bypass state.
        juce::String customName;        ///< User-defined channel label (empty = use plugin name).
        juce::Colour customColor { juce::Colours::transparentBlack }; ///< User-defined channel colour.
    };

    // ── Construction ──────────────────────────────────────────────────────────

    /**
     * @brief Constructs a ProjectSerializer.
     * @param bridges       Application-owned bridge array.  Must outlive this object.
     * @param audioEngine   The application audio engine.  Must outlive this object.
     * @param midiRouter    MIDI routing manager.  Must outlive this object.
     * @param deviceManager JUCE audio device manager.  Must outlive this object.
     * @param appProperties Persistent application properties store.  Must outlive this object.
     */
    ProjectSerializer (const juce::OwnedArray<BridgeInstance>& bridges,
                       AudioEngine&                             audioEngine,
                       MidiRoutingManager&                      midiRouter,
                       juce::AudioDeviceManager&                deviceManager,
                       juce::ApplicationProperties&             appProperties);

    // ── Callbacks — wired by LvhProApplication ────────────────────────────────

    /** @brief Called with a human-readable status string for the UI log. */
    std::function<void(const juce::String&)> onMessage;

    /**
     * @brief Request LvhProApplication to launch a bridge subprocess.
     *
     * Avoids a circular dependency: ProjectSerializer never holds a reference
     * to BridgeManager.
     *
     * @param file          Plugin file to open.
     * @param role          Instrument or Effect.
     * @param fxParentPath  Empty for instruments/master FX; parent path for per-channel FX.
     * @param isGlobal      true when the bridge belongs to the Global layer.
     */
    std::function<void(const juce::File&,
                       BridgeInstance::Role,
                       const juce::String& fxParentPath,
                       bool isGlobal)> onLaunchBridge;

    /**
     * @brief Called at the start of loadProject() to tear down the current graph.
     * @param keepGlobal  true = preserve Global-layer bridges (song switch);
     *                    false = full teardown.
     */
    std::function<void(bool keepGlobal)> onProjectResetRequired;

    // --- Save-time getters ---
    std::function<double()>               getMasterVolume;      ///< Returns the current master volume.
    std::function<juce::Rectangle<int>()> getCoreWindowBounds;  ///< Returns the main window bounds.
    std::function<bool()>                 getMixerVisible;       ///< Returns whether the mixer window is visible.
    std::function<juce::Rectangle<int>()> getMixerWindowBounds; ///< Returns the mixer window bounds.

    // --- Load-time setters ---
    std::function<void(double)>                     onMasterVolumeChanged;       ///< Restore master volume.
    std::function<void(juce::Rectangle<int>)>       onCoreWindowBoundsChanged;   ///< Restore main window position.
    std::function<void(bool, juce::Rectangle<int>)> onMixerWindowRestored;       ///< Show/hide + position mixer.
    /**
     * @brief Restore metronome settings after a project load.
     * @param bpm          Tempo in BPM.
     * @param volume       Click volume [0.0, 1.0].
     * @param beatsPerBar  Time signature numerator.
     * @param clickType    0 = Normal, 1 = Techno.
     */
    std::function<void(double bpm, float volume, int beatsPerBar, int clickType)> onMetronomeSettingsRestored;

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * @brief Save the current session to a `.lvh` project file.
     *
     * Collects plugin state from all connected bridges asynchronously,
     * then writes the XML project file.
     *
     * @param file  Destination file path.
     */
    void saveProject (const juce::File& file);

    /**
     * @brief Load a `.lvh` project file and restore the session.
     *
     * Behaviour depends on the @p isGlobal and @p globalLayerSwitch flags;
     * see the class-level documentation table for details.
     *
     * @param file               The `.lvh` file to open.
     * @param isGlobal           true when loading the Global (Slot 0) project.
     * @param globalLayerSwitch  true when switching songs within a Stage Set (Slot 1+).
     */
    void loadProject (const juce::File& file,
                      bool isGlobal          = false,
                      bool globalLayerSwitch = false);

    /**
     * @brief Returns the currently open project file.
     * @return The file, or an invalid File object if no project is open.
     */
    juce::File getCurrentProjectFile() const noexcept { return currentProjectFile_; }

    /**
     * @brief Set the current project file path without loading it.
     * @param f  The new current project file.
     */
    void setCurrentProjectFile (const juce::File& f) { currentProjectFile_ = f; }

    // ── Closure-injection helpers ─────────────────────────────────────────────

    /**
     * @brief Remove and return the pending plugin state for @p pluginPath.
     *
     * Call this in BridgeManager::launchBridgeWithPath() *before* spawning the
     * bridge process.  Returns an empty MemoryBlock if no pending data exists.
     *
     * @param pluginPath  Absolute plugin file path used as the key.
     * @return            The serialized plugin state, or an empty MemoryBlock.
     */
    juce::MemoryBlock takePendingState (const juce::String& pluginPath);

    /**
     * @brief Remove and return the pending mixer settings for @p pluginPath.
     * @param pluginPath  Absolute plugin file path used as the key.
     * @return            MixerSettings, or std::nullopt if no data exists.
     */
    std::optional<MixerSettings> takePendingMixer (const juce::String& pluginPath);

    /**
     * @brief Remove and return the pending editor window bounds for @p pluginPath.
     * @param pluginPath  Absolute plugin file path used as the key.
     * @return            Window bounds rectangle, or a zero rectangle if no data exists.
     */
    juce::Rectangle<int> takePendingBounds (const juce::String& pluginPath);

private:
    struct BridgeStateEntry
    {
        BridgeInstance*   bridge   = nullptr;
        juce::MemoryBlock state;
        bool              received = false;
    };

    void writeProjectXml (const juce::File& file,
                          const std::vector<BridgeStateEntry>& stateEntries);

    const juce::OwnedArray<BridgeInstance>& bridges_;
    AudioEngine&                             audioEngine_;
    MidiRoutingManager&                      midiRouter_;
    juce::AudioDeviceManager&                deviceManager_;
    juce::ApplicationProperties&             appProperties_;

    juce::File                                   currentProjectFile_;
    std::map<juce::String, juce::MemoryBlock>    pendingPluginStates_;
    std::map<juce::String, MixerSettings>        pendingMixerSettings_;
    std::map<juce::String, juce::Rectangle<int>> pendingWindowBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectSerializer)
};
