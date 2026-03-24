#pragma once
#include <JuceHeader.h>
#include "AudioEngine.h"
#include "BridgeManager.h"
#include "ProjectSerializer.h"
#include "MidiRoutingManager.h"
#include "StageManager.h"

// Forward declarations — full definitions only needed in UIManager.cpp
class MainComponent;
class MixerWindow;
class SettingsWindow;
class StageWindow;

// =====================================================================
// UIManager
//
// Owns all UI/window management extracted from LvhProApplication:
//   - MixerWindow lifecycle (toggleMixerWindow)
//   - SettingsWindow lifecycle (openSettings)
//   - Logo right-click popup menu (showMainMenu)
//   - Bridge file choosers (launchBridgeFileChooser)
//   - Volume/speaker button wiring (wired in setMainComponent)
//   - masterVolume_ state
//
// Dependencies injected via constructor:
//   audioEngine, bridgeManager, projectSerializer, midiRouter,
//   deviceManager, appProperties, knownPlugins
//
// MainComponent* is set post-construction via setMainComponent()
// (called from LvhProApplication::initialise after mainWindow is created).
// shutdown() must be called before mainWindow is destroyed.
// =====================================================================
class UIManager
{
public:
    // ── Construction ──────────────────────────────────────────────────────
    UIManager (AudioEngine&                  audioEngine,
               BridgeManager&                bridgeManager,
               ProjectSerializer&            projectSerializer,
               MidiRoutingManager&           midiRouter,
               juce::AudioDeviceManager&     deviceManager,
               juce::ApplicationProperties&  appProperties,
               juce::KnownPluginList&        knownPlugins,
               StageManager&                 stageManager);

    ~UIManager();

    // ── Callbacks — wired by LvhProApplication ────────────────────────────

    // Trigger plugin scan (still managed by LvhProApplication)
    std::function<void()> onStartPluginScan;

    // ── Lifecycle ─────────────────────────────────────────────────────────

    // Call after mainWindow is created. Wires volume/menu/mixer/launch callbacks.
    void setMainComponent (MainComponent* mc);

    // Call at start of LvhProApplication::shutdown() before mainWindow.reset().
    void shutdown();

    // ── Window management ─────────────────────────────────────────────────
    void toggleMixerWindow  (bool show);
    void toggleStageWindow  (bool show);
    void openSettings();

    // Restore StageWindow state (visibility + bounds) from ApplicationProperties.
    // Call once during app initialise, after all callbacks are wired.
    void restoreStageWindow();

    // ── Mixer bridge list (called from BridgeManager::onGraphRebuilt) ─────
    void updateMixerBridges (juce::Array<BridgeInstance*> instruments,
                             juce::Array<BridgeInstance*> effects);

    // ── Volume state (owned here; serializer queries via callbacks) ────────
    double getMasterVolume() const noexcept { return masterVolume_; }
    void   setMasterVolume (double vol);   // also updates engine, slider, mixer fader

    // ── Mixer window state queries (for ProjectSerializer callbacks) ───────
    bool                 isMixerWindowVisible()  const noexcept;
    juce::Rectangle<int> getMixerWindowBounds()  const noexcept;

    // Restore mixer window visibility + position from project load
    void restoreMixerWindow (bool visible, juce::Rectangle<int> bounds);

    // ── Bridge file choosers ───────────────────────────────────────────────
    void launchBridgeFileChooser (BridgeInstance::Role role = BridgeInstance::Role::Instrument);

private:
    // ── Private helpers ───────────────────────────────────────────────────
    void showMainMenu();
    void showPluginPicker (BridgeInstance::Role fixedRole, BridgeInstance* parentInstrument = nullptr);
    juce::File getBridgeStartDir() const;

    // Check isDirty(); if clean, run action() immediately.
    // If dirty, show Yes/No/Cancel dialog:
    //   Yes    → save the current set via FileChooser, then run action()
    //   No     → discard changes, run action()
    //   Cancel → abort
    void executeSafeSetOperation (std::function<void()> action);

    // ── Constructor-injected references ───────────────────────────────────
    AudioEngine&                 audioEngine_;
    BridgeManager&               bridgeManager_;
    ProjectSerializer&           projectSerializer_;
    MidiRoutingManager&          midiRouter_;
    juce::AudioDeviceManager&    deviceManager_;
    juce::ApplicationProperties& appProperties_;
    juce::KnownPluginList&       knownPlugins_;
    StageManager&                stageManager_;

    // ── State ─────────────────────────────────────────────────────────────
    MainComponent*                   mc_           = nullptr;
    double                           masterVolume_ = 1.0;
    std::unique_ptr<MixerWindow>     mixerWindow_;
    std::unique_ptr<SettingsWindow>  settingsWindow_;
    std::unique_ptr<StageWindow>     stageWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UIManager)
};
