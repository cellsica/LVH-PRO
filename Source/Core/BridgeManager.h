#pragma once
#include <JuceHeader.h>
#include <optional>
#include "BridgeInstance.h"
#include "AudioEngine.h"
#include "ProjectSerializer.h"

/**
 * @class BridgeManager
 * @brief Owns the lifetime of all BridgeInstance objects and the audio plugin graph.
 *
 * Responsibilities:
 * - Launch LVH-Bridge.exe subprocesses for each loaded plugin.
 * - Maintain the ordered list of BridgeInstance objects.
 * - Rebuild the JUCE audio graph whenever the bridge list changes.
 * - Manage the recent-bridge-files list in ApplicationProperties.
 *
 * **Dependency injection:** audioEngine, deviceManager, and appProperties are
 * passed via the constructor and must outlive this object.
 *
 * **MIDI routing** is handled through callbacks (onBridgeDisconnectedMidi,
 * onApplyPendingMidiTarget) to avoid a circular constructor dependency with
 * MidiRoutingManager, which itself holds a const-ref to the bridge array.
 *
 * **Project restore:** Pending plugin state, mixer settings, and window bounds
 * are passed directly into launchBridgeWithPath() as value arguments (closure
 * injection at the call site), so BridgeManager never calls back into
 * ProjectSerializer after returning.
 */
class BridgeManager : public juce::ChangeListener
{
public:
    /**
     * @brief Constructs a BridgeManager.
     * @param audioEngine    The application audio engine.  Must outlive this object.
     * @param deviceManager  JUCE audio device manager.  Must outlive this object.
     * @param appProperties  Persistent application properties store.  Must outlive this object.
     */
    BridgeManager (AudioEngine&                 audioEngine,
                   juce::AudioDeviceManager&    deviceManager,
                   juce::ApplicationProperties& appProperties);

    // ── Callbacks — wired by LvhProApplication ────────────────────────────────

    /** @brief Called with a human-readable status string for the UI log. */
    std::function<void(const juce::String&)> onMessage;

    /**
     * @brief Called after rebuildBridgeGraph() completes.
     *
     * Provides separate lists of instrument and effect bridges so that the
     * Mixer Console can update its channel strips.
     */
    std::function<void(juce::Array<BridgeInstance*> instruments,
                       juce::Array<BridgeInstance*> effects)> onGraphRebuilt;

    /**
     * @brief Called when a bridge disconnects.
     *
     * The handler should call MidiRoutingManager::handleBridgeDisconnected()
     * and update the UI if routing changed.
     */
    std::function<void(BridgeInstance*)> onBridgeDisconnectedMidi;

    /**
     * @brief Called when a bridge connects so routing can be restored.
     *
     * The handler should call MidiRoutingManager::tryApplyPendingTarget().
     *
     * @param pluginPath  Absolute path of the plugin that just connected.
     * @param b           The newly-connected BridgeInstance.
     */
    std::function<void(const juce::String& pluginPath, BridgeInstance*)> onApplyPendingMidiTarget;

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * @brief Launch an LVH-Bridge.exe subprocess for the given plugin file.
     *
     * Pending restore data is value-copied into the bridge's onConnected lambda
     * so this method never touches ProjectSerializer after returning.
     *
     * @param pluginFile     The VST3/CLAP plugin file to load.
     * @param role           Instrument or Effect role (affects audio graph routing).
     * @param pendingState   Serialized plugin state to restore on connect (may be empty).
     * @param pendingMixer   Mixer channel settings to restore on connect (may be nullopt).
     * @param pendingBounds  Floating plugin editor bounds to restore on connect (may be zero).
     * @param fxParentPath   For per-channel FX: the parent instrument's plugin path.
     *                       Empty for master instruments and global FX.
     * @param isGlobal       true if this bridge belongs to the Global project layer (Stage Set Slot 0).
     */
    void launchBridgeWithPath (
        const juce::File&                               pluginFile,
        BridgeInstance::Role                            role          = BridgeInstance::Role::Instrument,
        juce::MemoryBlock                               pendingState  = {},
        std::optional<ProjectSerializer::MixerSettings> pendingMixer  = std::nullopt,
        juce::Rectangle<int>                            pendingBounds = {},
        juce::String                                    fxParentPath  = {},
        bool                                            isGlobal      = false);

    /**
     * @brief Destroy all managed bridges.
     *
     * @param keepGlobal  If true, bridges whose isGlobal() returns true are kept alive.
     *                    Used when switching songs in a Stage Set so the Global layer persists.
     */
    void clearBridges (bool keepGlobal = false);

    /**
     * @brief Move a bridge to a new position and rebuild the audio graph.
     *
     * Used by UIManager to reorder FX slots in the Mixer Console.
     *
     * @param b         The bridge to move.
     * @param newIndex  Target position in the bridges array.
     */
    void moveBridge (BridgeInstance* b, int newIndex);

    /**
     * @brief Returns read-only access to the bridge array.
     *
     * Used by MidiRoutingManager and ProjectSerializer which hold a const-ref
     * to this array and must not modify it.
     *
     * @return Const reference to the OwnedArray of BridgeInstance objects.
     */
    const juce::OwnedArray<BridgeInstance>& getBridges() const noexcept { return bridges_; }

    /**
     * @brief Returns the list of recently used bridge files from ApplicationProperties.
     * @return Array of recently opened plugin files, most-recent first.
     */
    juce::Array<juce::File> getRecentBridgeFiles() const;

    /**
     * @brief Add a file to the top of the recent-bridge-files list.
     *
     * Duplicate entries are removed and the list is trimmed to a fixed maximum length.
     *
     * @param file  The plugin file to add.
     */
    void addToRecentBridgeFiles (const juce::File& file);

    /**
     * @brief juce::ChangeListener callback — fired when AudioDeviceManager settings change.
     *
     * Rebuilds the audio graph so all bridges use the updated device configuration.
     */
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    void rebuildBridgeGraph();

    AudioEngine&                 audioEngine_;
    juce::AudioDeviceManager&    deviceManager_;
    juce::ApplicationProperties& appProperties_;

    juce::OwnedArray<BridgeInstance> bridges_;  ///< Owned bridge instances.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeManager)
};
