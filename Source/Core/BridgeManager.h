#pragma once
#include <JuceHeader.h>
#include <optional>
#include "BridgeInstance.h"
#include "AudioEngine.h"
#include "ProjectSerializer.h"

// =====================================================================
// BridgeManager
//
// Owns all BridgeInstance lifetime management extracted from LvhProApplication:
//   - juce::OwnedArray<BridgeInstance> bridges  (ownership)
//   - launchBridgeWithPath / rebuildBridgeGraph
//   - getRecentBridgeFiles / addToRecentBridgeFiles
//   - LVH-Bridge.exe path resolution
//
// Dependencies injected via constructor:
//   audioEngine, deviceManager, appProperties
//
// MIDI routing is handled through callbacks (onBridgeDisconnectedMidi,
// onApplyPendingMidiTarget) to avoid circular constructor dependency with
// MidiRoutingManager (which itself holds a const-ref to bridges_).
//
// Pending project-restore data (state, mixer settings, window bounds) is
// passed directly into launchBridgeWithPath as value arguments (closure
// injection at call site in LvhProApplication), so BridgeManager never
// calls back into ProjectSerializer.
// =====================================================================
class BridgeManager
{
public:
    // ── Construction ──────────────────────────────────────────────────────
    BridgeManager (AudioEngine&              audioEngine,
                   juce::AudioDeviceManager& deviceManager,
                   juce::ApplicationProperties& appProperties);

    // ── Callbacks — wired by LvhProApplication ────────────────────────────

    // General system message (UI log)
    std::function<void(const juce::String&)> onMessage;

    // Called after rebuildBridgeGraph completes (used to update Mixer Console)
    std::function<void(juce::Array<BridgeInstance*> instruments,
                       juce::Array<BridgeInstance*> effects)> onGraphRebuilt;

    // Called when a bridge disconnects — should call midiRouter.handleBridgeDisconnected(b)
    std::function<void(BridgeInstance*)> onBridgeDisconnectedMidi;

    // Called in onConnected — should call midiRouter.tryApplyPendingTarget(path, b)
    std::function<void(const juce::String& pluginPath, BridgeInstance*)> onApplyPendingMidiTarget;

    // ── Public API ────────────────────────────────────────────────────────

    // Launch a bridge with optional project-restore data (closure injection).
    // Pending data is value-copied into the onConnected lambda so this method
    // never touches ProjectSerializer after returning.
    void launchBridgeWithPath (
        const juce::File&                             pluginFile,
        BridgeInstance::Role                          role          = BridgeInstance::Role::Instrument,
        juce::MemoryBlock                             pendingState  = {},
        std::optional<ProjectSerializer::MixerSettings> pendingMixer = std::nullopt,
        juce::Rectangle<int>                          pendingBounds = {},
        juce::String                                  fxParentPath  = {},
        bool                                          isGlobal      = false);

    // Destroy bridges. When keepGlobal=true, bridges with isGlobal()==true are kept alive
    // (used when switching songs within a Stage Set so the Global layer persists).
    void clearBridges (bool keepGlobal = false);

    // Read-only access to the bridge array (for MidiRoutingManager / ProjectSerializer).
    const juce::OwnedArray<BridgeInstance>& getBridges() const noexcept { return bridges_; }

    // Recent bridge file list (stored in appProperties).
    juce::Array<juce::File> getRecentBridgeFiles() const;
    void addToRecentBridgeFiles (const juce::File& file);

private:
    // ── Private helpers ───────────────────────────────────────────────────
    void rebuildBridgeGraph();

    // ── Constructor-injected references ───────────────────────────────────
    AudioEngine&                 audioEngine_;
    juce::AudioDeviceManager&    deviceManager_;
    juce::ApplicationProperties& appProperties_;

    // ── State ─────────────────────────────────────────────────────────────
    juce::OwnedArray<BridgeInstance> bridges_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeManager)
};
