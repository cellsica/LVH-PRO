#include "BridgeManager.h"
#include <map>

BridgeManager::BridgeManager (AudioEngine&              audioEngine,
                               juce::AudioDeviceManager& deviceManager,
                               juce::ApplicationProperties& appProperties)
    : audioEngine_  (audioEngine),
      deviceManager_(deviceManager),
      appProperties_(appProperties)
{}

// ── Public API ────────────────────────────────────────────────────────────

void BridgeManager::launchBridgeWithPath (
    const juce::File&                              pluginFile,
    BridgeInstance::Role                           role,
    juce::MemoryBlock                              pendingState,
    std::optional<ProjectSerializer::MixerSettings> pendingMixer,
    juce::Rectangle<int>                           pendingBounds,
    juce::String                                   fxParentPath,
    bool                                           isGlobal)
{
    auto bridgeExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                         .getParentDirectory()
                         .getChildFile ("LVH-Bridge.exe");

    if (! bridgeExe.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Bridge Error", "LVH-Bridge.exe not found.");
        return;
    }

    auto* bridge = bridges_.add (new BridgeInstance());
    bridge->setRole (role);
    bridge->setIsGlobal (isGlobal);
    if (fxParentPath.isNotEmpty())
        bridge->setFxParentPath (fxParentPath);
    juce::String pluginName = pluginFile.getFileNameWithoutExtension()
                              + (role == BridgeInstance::Role::Effect ? " [FX]" : "");
    juce::String pluginPathStr = pluginFile.getFullPathName();

    // All pending data is captured by value — onConnected never touches BridgeManager
    // state outside of the bridges_ array (via rebuildBridgeGraph) and callbacks.
    bridge->onConnected = [this, pluginName, pluginPathStr,
                            pendingState  = std::move (pendingState),
                            pendingMixer,
                            pendingBounds] (BridgeInstance* b) mutable {
        auto& setup = deviceManager_.getAudioDeviceSetup();
        auto sr = static_cast<float> (setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0);
        auto bs = setup.bufferSize > 0 ? setup.bufferSize : 512;

        // Send SetState BEFORE AudioConfig so the Bridge can apply it
        // before prepareToPlay (correct VST3 restore order: setState → prepareToPlay).
        if (pendingState.getSize() > 0)
        {
            if (onMessage)
                onMessage ("Restoring plugin state for: " + pluginName
                    + " (" + juce::String ((int) pendingState.getSize()) + " bytes)");
            b->sendSetState (pendingState);
        }

        // Restore mixer settings before rebuildBridgeGraph so MixerStrip reads correct values
        if (pendingMixer.has_value())
        {
            const auto& ms = *pendingMixer;
            b->mixerGain.store     (ms.gain,     std::memory_order_relaxed);
            b->mixerPan.store      (ms.pan,      std::memory_order_relaxed);
            b->mixerMuted.store    (ms.muted,    std::memory_order_relaxed);
            b->mixerBypassed.store (ms.bypassed, std::memory_order_relaxed);
            b->mixerCustomName  = ms.customName;
            b->mixerCustomColor = ms.customColor;
        }

        b->sendAudioConfig (sr, bs);

        // Update Bridge window title:
        //   Instruments:       "LVH-Bridge [StripName]"
        //   Master FX:         "LVH-Bridge [MASTER]: [FxName]"
        //   Per-channel FX:    "LVH-Bridge [ParentName]: [FxName]"
        {
            juce::String displayName = b->mixerCustomName.isNotEmpty()
                                       ? b->mixerCustomName
                                       : juce::File (pluginPathStr).getFileNameWithoutExtension();
            juce::String title;
            if (b->getRole() == BridgeInstance::Role::Effect)
            {
                juce::String parentLabel = "MASTER";
                if (b->getFxParentPath().isNotEmpty())
                    parentLabel = juce::File (b->getFxParentPath()).getFileNameWithoutExtension();
                title = "LVH-Bridge [" + parentLabel + "]: [" + displayName + "]";
            }
            else
            {
                title = "LVH-Bridge [" + displayName + "]";
            }
            b->sendWindowTitle (title);
        }

        if (onMessage) onMessage ("Bridge connected: " + pluginName);
        juce::MessageManager::callAsync ([this] { rebuildBridgeGraph(); });

        // Restore window position from project load
        if (pendingBounds.getWidth() > 0 && pendingBounds.getHeight() > 0)
        {
            juce::Timer::callAfterDelay (800, [b, pendingBounds] {
                b->sendWindowPos (pendingBounds.getX(), pendingBounds.getY(),
                                  pendingBounds.getWidth(), pendingBounds.getHeight());
            });
        }

        // Restore MIDI routing target
        if (onApplyPendingMidiTarget) onApplyPendingMidiTarget (pluginPathStr, b);
    };

    bridge->onDisconnected = [this, pluginName] (BridgeInstance* b) {
        juce::MessageManager::callAsync ([this, b, pluginName] {
            if (onMessage) onMessage ("Bridge disconnected: " + pluginName);
            // If this bridge was the solo MIDI target, fall back to All mode.
            if (onBridgeDisconnectedMidi) onBridgeDisconnectedMidi (b);
            // Rebuild the graph BEFORE removing the bridge (state is already Idle,
            // so this bridge is excluded from the active list automatically).
            rebuildBridgeGraph();
            bridges_.removeObject (b);
        });
    };

    if (onMessage) onMessage ("Launching bridge: " + pluginName);
    bridge->launch (pluginFile.getFullPathName(), bridgeExe);

    addToRecentBridgeFiles (pluginFile);
    if (auto* prefs = appProperties_.getUserSettings())
        if (prefs->getBoolValue ("rememberLastFolder", true))
            prefs->setValue ("lastBridgeFolder",
                             pluginFile.getParentDirectory().getFullPathName());
}

void BridgeManager::clearBridges (bool keepGlobal)
{
    if (! keepGlobal)
    {
        bridges_.clear();
        return;
    }

    // Remove only non-global bridges (iterate in reverse to avoid index shifts).
    for (int i = bridges_.size() - 1; i >= 0; --i)
        if (! bridges_[i]->isGlobal())
            bridges_.remove (i);
}

juce::Array<juce::File> BridgeManager::getRecentBridgeFiles() const
{
    auto* prefs = appProperties_.getUserSettings();
    if (prefs == nullptr) return {};
    const int maxDisplay = prefs->getIntValue ("recentBridgeCount", 5);
    auto parts = juce::StringArray::fromTokens (prefs->getValue ("recentBridgeFiles"), "|", "");
    juce::Array<juce::File> result;
    for (int i = 0; i < juce::jmin (maxDisplay, parts.size()); ++i)
        if (parts[i].isNotEmpty())
            result.add (juce::File (parts[i]));
    return result;
}

void BridgeManager::addToRecentBridgeFiles (const juce::File& file)
{
    auto* prefs = appProperties_.getUserSettings();
    if (prefs == nullptr) return;
    auto parts = juce::StringArray::fromTokens (prefs->getValue ("recentBridgeFiles"), "|", "");
    parts.removeString (file.getFullPathName());
    parts.insert (0, file.getFullPathName());
    while (parts.size() > 20) parts.remove (parts.size() - 1);
    prefs->setValue ("recentBridgeFiles", parts.joinIntoString ("|"));
}

// ── Private ───────────────────────────────────────────────────────────────

void BridgeManager::rebuildBridgeGraph()
{
    juce::Array<BridgeInstance*> instruments, allEffects;

    // Local bridges first, global last — global instruments/effects sit
    // downstream in the mix chain (茜's routing advice).
    for (int pass = 0; pass < 2; ++pass)
    {
        bool wantGlobal = (pass == 1);
        for (auto* b : bridges_)
        {
            if (b->getState() != BridgeInstance::State::Connected) continue;
            if (b->isGlobal() != wantGlobal) continue;
            if (b->getRole() == BridgeInstance::Role::Effect)
                allEffects.add (b);
            else
                instruments.add (b);
        }
    }

    // Split effects into master chain and per-instrument FX chains
    juce::Array<BridgeInstance*> masterEffects;
    std::map<BridgeInstance*, juce::Array<BridgeInstance*>> perChannelFxMap;

    for (auto* fx : allEffects)
    {
        if (fx->getFxParentPath().isEmpty())
        {
            masterEffects.add (fx);
        }
        else
        {
            juce::File parentFile (fx->getFxParentPath());
            for (auto* instr : instruments)
                if (juce::File (instr->getPluginPath()) == parentFile)
                {
                    perChannelFxMap[instr].add (fx);
                    break;
                }
        }
    }

    audioEngine_.rebuildBridgeGraph (instruments, masterEffects, perChannelFxMap);
    // Pass all effects so the mixer UI can display both master and per-channel slots
    if (onGraphRebuilt) onGraphRebuilt (instruments, allEffects);
}
