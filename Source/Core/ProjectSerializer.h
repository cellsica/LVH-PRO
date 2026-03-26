#pragma once
#include <JuceHeader.h>
#include <optional>
#include "BridgeInstance.h"
#include "MidiRoutingManager.h"
#include "AudioEngine.h"

// =====================================================================
// ProjectSerializer
//
// Owns all .lvh project file I/O extracted from LvhProApplication:
//   - saveProject / loadProject / writeProjectXml
//   - pending state maps (states, mixer settings, window bounds)
//   - currentProjectFile
//
// Dependencies injected via constructor (all must outlive this object):
//   bridges, audioEngine, midiRouter, deviceManager, appProperties
//
// UI interactions are handled through callbacks set by LvhProApplication.
// Bridge launching uses the "closure injection" pattern: pending data is
// consumed at launch time (takePending*), captured in onConnected lambdas,
// so onConnected never calls back into ProjectSerializer.
// =====================================================================
class ProjectSerializer
{
public:
    // ── Nested types ──────────────────────────────────────────────────────
    struct MixerSettings
    {
        float        gain     = 1.f;
        float        pan      = 0.f;
        bool         muted    = false;
        bool         bypassed = false;
        juce::String customName;
        juce::Colour customColor { juce::Colours::transparentBlack };
    };

    // ── Construction ──────────────────────────────────────────────────────
    ProjectSerializer (const juce::OwnedArray<BridgeInstance>& bridges,
                       AudioEngine&                             audioEngine,
                       MidiRoutingManager&                      midiRouter,
                       juce::AudioDeviceManager&                deviceManager,
                       juce::ApplicationProperties&             appProperties);

    // ── Callbacks — wired by LvhProApplication ────────────────────────────

    // General system message (UI log)
    std::function<void(const juce::String&)> onMessage;

    // Request LvhProApplication to launch a bridge (avoids circular dependency).
    // fxParentPath is empty for instruments and master effects; non-empty for per-channel FX.
    // isGlobal=true when this bridge belongs to a Global project (Stage Set Slot 0).
    std::function<void(const juce::File&, BridgeInstance::Role, const juce::String& fxParentPath, bool isGlobal)> onLaunchBridge;

    // Called at the start of loadProject: tear down graph + selectively clear bridges.
    // keepGlobal=true when switching songs (Slot 1+): Global bridges are preserved.
    // keepGlobal=false for a full reset (Slot 0 reload, New, direct file open).
    std::function<void(bool keepGlobal)> onProjectResetRequired;

    // --- Save-time getters ---
    std::function<double()>               getMasterVolume;       // current master volume
    std::function<juce::Rectangle<int>()> getCoreWindowBounds;   // main window bounds
    std::function<bool()>                 getMixerVisible;        // is mixer window visible?
    std::function<juce::Rectangle<int>()> getMixerWindowBounds;  // mixer window bounds

    // --- Load-time setters ---
    std::function<void(double)>                          onMasterVolumeChanged;   // restore volume
    std::function<void(juce::Rectangle<int>)>            onCoreWindowBoundsChanged;
    // visible=true  → show + position mixer window
    // visible=false → hide mixer window
    std::function<void(bool, juce::Rectangle<int>)>      onMixerWindowRestored;

    // ── Public API ────────────────────────────────────────────────────────
    void saveProject (const juce::File& file);

    // isGlobal=true,  globalLayerSwitch=false → Slot 0: full reset, all bridges marked Global.
    // isGlobal=false, globalLayerSwitch=true  → Slot 1+: instrument-only switch.
    //   - Settings (window pos/size, master volume, MIDI routing) are inherited from Slot 0 (skipped).
    //   - Master-chain FX from Slot 0 are kept; only Instrument bridges (+ per-channel FX) are replaced.
    //   - Master-chain FX entries in the .lvh are skipped (Slot 0's are already loaded).
    // isGlobal=false, globalLayerSwitch=false → direct file open: full reset, no Global marking.
    void loadProject (const juce::File& file, bool isGlobal = false, bool globalLayerSwitch = false);

    juce::File getCurrentProjectFile() const noexcept { return currentProjectFile_; }
    void       setCurrentProjectFile (const juce::File& f) { currentProjectFile_ = f; }

    // ── Closure-injection helpers ─────────────────────────────────────────
    // Call these in launchBridgeWithPath BEFORE spawning the bridge.
    // Each method removes and returns the stored data (take = move + erase).
    // Returns empty / default-constructed value if no pending data exists.
    juce::MemoryBlock            takePendingState  (const juce::String& pluginPath);
    std::optional<MixerSettings> takePendingMixer  (const juce::String& pluginPath);
    juce::Rectangle<int>         takePendingBounds (const juce::String& pluginPath);

private:
    // ── Private helpers ───────────────────────────────────────────────────
    struct BridgeStateEntry
    {
        BridgeInstance*  bridge   = nullptr;
        juce::MemoryBlock state;
        bool             received = false;
    };

    void writeProjectXml (const juce::File& file,
                          const std::vector<BridgeStateEntry>& stateEntries);

    // ── Constructor-injected references ───────────────────────────────────
    const juce::OwnedArray<BridgeInstance>& bridges_;
    AudioEngine&                             audioEngine_;
    MidiRoutingManager&                      midiRouter_;
    juce::AudioDeviceManager&                deviceManager_;
    juce::ApplicationProperties&             appProperties_;

    // ── State ─────────────────────────────────────────────────────────────
    juce::File                                       currentProjectFile_;
    std::map<juce::String, juce::MemoryBlock>        pendingPluginStates_;
    std::map<juce::String, MixerSettings>            pendingMixerSettings_;
    std::map<juce::String, juce::Rectangle<int>>     pendingWindowBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectSerializer)
};
