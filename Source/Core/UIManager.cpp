#include "UIManager.h"
#include "MainComponent.h"
#include "MixerWindow.h"
#include "SettingsWindow.h"
#include "StageWindow.h"
#include "../LanguageManager.h"

UIManager::UIManager (AudioEngine&                  audioEngine,
                       BridgeManager&                bridgeManager,
                       ProjectSerializer&            projectSerializer,
                       MidiRoutingManager&           midiRouter,
                       juce::AudioDeviceManager&     deviceManager,
                       juce::ApplicationProperties&  appProperties,
                       juce::KnownPluginList&        knownPlugins,
                       StageManager&                 stageManager)
    : audioEngine_      (audioEngine),
      bridgeManager_    (bridgeManager),
      projectSerializer_(projectSerializer),
      midiRouter_       (midiRouter),
      deviceManager_    (deviceManager),
      appProperties_    (appProperties),
      knownPlugins_     (knownPlugins),
      stageManager_     (stageManager)
{
    vuPhysicsEngine_ = std::make_unique<VUPhysicsEngine> (
        [this] (int ch) { return audioEngine_.exchangeRms (ch); });
}

UIManager::~UIManager() = default;

// ── Lifecycle ─────────────────────────────────────────────────────────────

void UIManager::setMainComponent (MainComponent* mc)
{
    mc_ = mc;
    if (mc == nullptr) return;

    // Restore saved language before any window is created
    LanguageManager::getInstance().init (appProperties_.getUserSettings());

    // Load persisted MIDI mappings for Mixer channels
    loadMixerMappings();

    // Speaker mute button + volume slider (share savedGain)
    auto savedGain = std::make_shared<double> (1.0);

    mc_->getSpeakerButton().onClick = [this, mc, savedGain] {
        bool muted = mc->getSpeakerButton().getToggleState();
        if (muted)
        {
            *savedGain = mc->getVolumeSlider().getValue();
            mc->getVolumeSlider().setValue (0.0, juce::sendNotificationSync);
        }
        else
        {
            mc->getVolumeSlider().setValue (*savedGain > 0.0 ? *savedGain : 1.0,
                                            juce::sendNotificationSync);
        }
    };

    mc_->onVolumeChanged = [this, mc, savedGain] (double v) {
        masterVolume_ = v;
        audioEngine_.setOutputGain ((float) v);
        if (mixerWindow_ != nullptr) mixerWindow_->setMasterGain ((float) v);
        // If slider is moved away from 0 while muted, auto-unmute
        if (v > 0.0 && mc->getSpeakerButton().getToggleState())
        {
            *savedGain = v;
            mc->getSpeakerButton().setToggleState (false, juce::dontSendNotification);
        }
    };

    mc_->onLogoRightClick      = [this] { showMainMenu(); };
    mc_->onMixerToggle         = [this] (bool show) { toggleMixerWindow     (show); };
    mc_->onStageToggle         = [this] (bool show) { toggleStageWindow     (show); };
    mc_->onMetronomeToggle     = [this] (bool show) { toggleMetronomeWindow (show); };
    mc_->onVuMeterToggle       = [this] (bool show) { toggleVuMeterWindow   (show); };
    mc_->onLaunchBridgeClicked = [this] { launchBridgeFileChooser(); };

    // Beat callback: fires on message thread (via AsyncUpdater in MetronomeProcessor)
    audioEngine_.setMetronomeOnBeat ([this] (int beat) {
        if (metronomeWindow_ != nullptr && metronomeWindow_->isVisible())
            metronomeWindow_->updateBeat (beat);
    });

    // Restore persisted metronome click type
    if (auto* prefs = appProperties_.getUserSettings())
    {
        int ct = prefs->getIntValue ("metronomeClickType", 0);
        audioEngine_.setMetronomeClickType (
            ct == 1 ? MetronomeProcessor::ClickType::Techno
                    : MetronomeProcessor::ClickType::Normal);
    }

    updateMidiDeviceLabel();
}

void UIManager::updateMidiDeviceLabel()
{
    if (mc_ == nullptr) return;
    for (auto& d : juce::MidiInput::getAvailableDevices())
    {
        if (deviceManager_.isMidiInputDeviceEnabled (d.identifier))
        {
            mc_->setMidiMonitorText ("MIDI IN: " + d.name);
            return;
        }
    }
    mc_->setMidiMonitorText ("No MIDI");
}

void UIManager::shutdown()
{
    // Persist StageWindow and MixerWindow state before destroying them
    if (auto* prefs = appProperties_.getUserSettings())
    {
        bool stageVisible = stageWindow_ != nullptr && stageWindow_->isVisible();
        prefs->setValue ("stageWindowVisible", stageVisible);
        if (stageWindow_ != nullptr)
        {
            auto b = stageWindow_->getBounds();
            prefs->setValue ("stageWindowX",       b.getX());
            prefs->setValue ("stageWindowY",       b.getY());
            prefs->setValue ("stageWindowW",       b.getWidth());
            prefs->setValue ("stageWindowH",       b.getHeight());
            prefs->setValue ("stageAlwaysOnTop",   stageWindow_->isAlwaysOnTop());
        }

        if (mixerWindow_ != nullptr)
            prefs->setValue ("mixerAlwaysOnTop", mixerWindow_->isAlwaysOnTop());
        if (metronomeWindow_ != nullptr)
            prefs->setValue ("metronomeAlwaysOnTop", metronomeWindow_->isAlwaysOnTop());
    }

    stageWindow_.reset();
    settingsWindow_.reset();
    mixerWindow_.reset();
    metronomeWindow_.reset();
    vuMeterWindow_.reset();
    mc_ = nullptr;
}

// ── Volume ────────────────────────────────────────────────────────────────

void UIManager::setMasterVolume (double vol)
{
    masterVolume_ = vol;
    audioEngine_.setOutputGain ((float) vol);
    if (mc_ != nullptr)
    {
        mc_->getVolumeSlider().setValue (vol, juce::dontSendNotification);
        mc_->setVolumeDisplay (vol);
        mc_->pushSystemMessage ("  masterVol loaded: " + juce::String (vol, 3));
    }
    if (mixerWindow_ != nullptr) mixerWindow_->setMasterGain ((float) vol);
}

// ── Mixer window ──────────────────────────────────────────────────────────

void UIManager::toggleMixerWindow (bool show)
{
    if (show)
    {
        if (mixerWindow_ == nullptr)
        {
            mixerWindow_ = std::make_unique<MixerWindow> ("Mixer Console");

            mixerWindow_->onClose = [this] {
                if (mc_ != nullptr) mc_->setMixerWindowVisible (false);
                mixerWindow_->setVisible (false);
            };

            mixerWindow_->onMasterGainChange = [this] (float v) {
                masterVolume_ = (double) v;
                audioEngine_.setOutputGain (v);
                if (mc_ != nullptr)
                {
                    mc_->getVolumeSlider().setValue ((double) v, juce::dontSendNotification);
                    mc_->setVolumeDisplay ((double) v);
                }
            };

            mixerWindow_->onToggleFxWindow = [] (BridgeInstance* b) {
                if (b->getState() != BridgeInstance::State::Connected)
                    return;  // bridge already disconnected — do not write to dead pipe
                auto bounds = b->getWindowBounds();
                if (bounds.getWidth() > 0 && bounds.getHeight() > 0)
                    b->sendWindowPos (bounds.getX(), bounds.getY(),
                                      bounds.getWidth(), bounds.getHeight());
            };

            mixerWindow_->onAddFx = [this] (BridgeInstance* parent) {
                showPluginPicker (BridgeInstance::Role::Effect, parent);
            };

            mixerWindow_->onMidiLearnRequest = [this] (BridgeInstance* b, MixerParam p) {
                startMidiLearn (b, p);
            };
            mixerWindow_->onMidiClearMapping = [this] (BridgeInstance* b, MixerParam p) {
                clearMidiMapping (b, p);
            };
        }

        // Restore pin state
        if (auto* prefs = appProperties_.getUserSettings())
        {
            bool pinned = prefs->getBoolValue ("mixerAlwaysOnTop", false);
            mixerWindow_->setPinState (pinned);
        }

        // Sync MASTER fader to current Core slider value
        if (mc_ != nullptr)
            mixerWindow_->setMasterGain ((float) mc_->getVolumeSlider().getValue());

        // Populate with the currently connected bridges split by role
        juce::Array<BridgeInstance*> instruments, effects;
        for (auto* b : bridgeManager_.getBridges())
        {
            if (b->getState() != BridgeInstance::State::Connected) continue;
            if (b->getRole() == BridgeInstance::Role::Effect)
                effects.add (b);
            else
                instruments.add (b);
        }
        mixerWindow_->updateBridges (instruments, effects);

        mixerWindow_->setVisible (true);
        mixerWindow_->toFront (true);
    }
    else
    {
        if (mixerWindow_ != nullptr)
            mixerWindow_->setVisible (false);
    }
}

// ── Metronome window ──────────────────────────────────────────────────────

void UIManager::toggleMetronomeWindow (bool show)
{
    // Icon clicked while metronome is playing in background (window hidden):
    // toggle went OFF → treat as re-open request instead
    if (!show && audioEngine_.isMetronomePlaying()
        && metronomeWindow_ != nullptr && !metronomeWindow_->isVisible())
    {
        show = true;
        if (mc_ != nullptr) mc_->setMetronomeWindowVisible (true);  // restore button ON
    }

    if (show)
    {
        if (metronomeWindow_ == nullptr)
        {
            metronomeWindow_ = std::make_unique<MetronomeWindow>();

            metronomeWindow_->onClose = [this] {
                metronomeWindow_->setVisible (false);
                // Keep toolbar button lit if metronome is still playing
                if (mc_ != nullptr)
                    mc_->setMetronomeWindowVisible (audioEngine_.isMetronomePlaying());
            };
            metronomeWindow_->onPlayStopChanged = [this] (bool playing) {
                audioEngine_.setMetronomePlaying (playing);
                // If window is hidden and playback stopped, turn off toolbar button
                if (!playing && mc_ != nullptr
                    && (metronomeWindow_ == nullptr || !metronomeWindow_->isVisible()))
                    mc_->setMetronomeWindowVisible (false);
            };
            metronomeWindow_->onBpmChanged = [this] (double bpm) {
                audioEngine_.setMetronomeBpm (bpm);
            };
            metronomeWindow_->onVolumeChanged = [this] (float v) {
                audioEngine_.setMetronomeVolume (v);
            };
            metronomeWindow_->onTapTempo = [this] {
                audioEngine_.tapMetronomeTempo();
                // Sync the new BPM back to the window
                if (metronomeWindow_ != nullptr)
                    metronomeWindow_->setBpm (audioEngine_.getMetronomeBpm());
            };
            metronomeWindow_->onBeatsPerBarChanged = [this] (int b) {
                audioEngine_.setMetronomeBeatsPerBar (b);
            };
            metronomeWindow_->wireCallbacks();
        }

        // Restore pin state
        if (auto* prefs = appProperties_.getUserSettings())
            metronomeWindow_->setPinState (prefs->getBoolValue ("metronomeAlwaysOnTop", false));

        // Sync current engine state to window
        metronomeWindow_->setPlayState   (audioEngine_.isMetronomePlaying());
        metronomeWindow_->setBpm         (audioEngine_.getMetronomeBpm());
        metronomeWindow_->setVolume      (audioEngine_.getMetronomeVolume());
        metronomeWindow_->setBeatsPerBar (audioEngine_.getMetronomeBeatsPerBar());

        metronomeWindow_->setVisible (true);
        metronomeWindow_->toFront (true);
    }
    else
    {
        if (metronomeWindow_ != nullptr)
            metronomeWindow_->setVisible (false);
    }
}

void UIManager::toggleVuMeterWindow (bool show)
{
    if (show)
    {
        if (vuMeterWindow_ == nullptr)
        {
            vuMeterWindow_ = std::make_unique<VUMeterWindow> (*vuPhysicsEngine_);
            vuPhysicsEngine_->start();
        }
        vuMeterWindow_->show();
        if (mc_ != nullptr) mc_->setVuMeterWindowVisible (true);
    }
    else
    {
        if (vuMeterWindow_ != nullptr)
            vuMeterWindow_->hide();
        if (mc_ != nullptr) mc_->setVuMeterWindowVisible (false);
    }
}

void UIManager::updateMixerBridges (juce::Array<BridgeInstance*> instruments,
                                     juce::Array<BridgeInstance*> effects)
{
    if (mixerWindow_ != nullptr)
        mixerWindow_->updateBridges (instruments, effects);
}

bool UIManager::isMixerWindowVisible() const noexcept
{
    return mixerWindow_ != nullptr && mixerWindow_->isVisible();
}

juce::Rectangle<int> UIManager::getMixerWindowBounds() const noexcept
{
    return mixerWindow_ != nullptr ? mixerWindow_->getBounds() : juce::Rectangle<int>{};
}

void UIManager::restoreMixerWindow (bool visible, juce::Rectangle<int> bounds)
{
    if (visible)
    {
        toggleMixerWindow (true);
        if (mixerWindow_ != nullptr && bounds.getWidth() > 100 && bounds.getHeight() > 50)
            mixerWindow_->setBounds (bounds);
        if (mc_ != nullptr) mc_->setMixerWindowVisible (true);
    }
    else
    {
        if (mixerWindow_ != nullptr) mixerWindow_->setVisible (false);
        if (mc_ != nullptr) mc_->setMixerWindowVisible (false);
    }
}

void UIManager::syncMetronomeWindowFromEngine()
{
    if (metronomeWindow_ == nullptr || !metronomeWindow_->isVisible()) return;
    metronomeWindow_->setBpm         (audioEngine_.getMetronomeBpm());
    metronomeWindow_->setVolume      (audioEngine_.getMetronomeVolume());
    metronomeWindow_->setBeatsPerBar (audioEngine_.getMetronomeBeatsPerBar());
}

// ── Stage window ──────────────────────────────────────────────────────────

void UIManager::toggleStageWindow (bool show)
{
    if (show)
    {
        if (stageWindow_ == nullptr)
        {
            stageWindow_ = std::make_unique<StageWindow> ("Stage Performance Mode", stageManager_);

            stageWindow_->onClose = [this] {
                stageWindow_->setVisible (false);
                if (mc_ != nullptr) mc_->setStageWindowVisible (false);
            };

            // Load a project item
            stageWindow_->setOnItemLoad ([this] (int idx) {
                stageManager_.loadItem (idx);
            });

            // Add .lvh file to the set
            stageWindow_->setOnAdd ([this] {
                auto chooser = std::make_shared<juce::FileChooser> (
                    LvhStr ("STR_ADD_BRIDGE_DIALOG"),
                    juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                    "*.lvh");
                chooser->launchAsync (
                    juce::FileBrowserComponent::openMode |
                    juce::FileBrowserComponent::canSelectFiles,
                    [this, chooser] (const juce::FileChooser& fc) {
                        auto f = fc.getResult();
                        if (f.existsAsFile())
                        {
                            stageManager_.addItem (f.getFileNameWithoutExtension(),
                                                   f.getFullPathName());
                        }
                    });
            });

            // Save .stg file
            stageWindow_->setOnSaveSet ([this] {
                auto curFile = stageManager_.getCurrentFile();
                auto chooser = std::make_shared<juce::FileChooser> (
                    LvhStr ("STR_SAVE_SET_DIALOG"),
                    curFile.existsAsFile()
                        ? curFile   // pre-fill filename for easy overwrite
                        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                    "*.stg");
                chooser->launchAsync (
                    juce::FileBrowserComponent::saveMode |
                    juce::FileBrowserComponent::canSelectFiles |
                    juce::FileBrowserComponent::warnAboutOverwriting,
                    [this, chooser] (const juce::FileChooser& fc) {
                        auto f = fc.getResult();
                        if (f.getFullPathName().isNotEmpty())
                        {
                            auto stgFile = f.withFileExtension ("stg");
                            stageManager_.saveSet (stgFile);
                            if (stageWindow_ != nullptr)
                            {
                                stageWindow_->refresh();   // update title (* disappears)
                                stageWindow_->setStatus ("Saved: " + stgFile.getFullPathName());
                            }
                        }
                    });
            });

            // Open .stg file — dirty-check first
            stageWindow_->setOnLoadSet ([this] {
                executeSafeSetOperation ([this] {
                    auto chooser = std::make_shared<juce::FileChooser> (
                        LvhStr ("STR_OPEN_SET_DIALOG"),
                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                        "*.stg");
                    chooser->launchAsync (
                        juce::FileBrowserComponent::openMode |
                        juce::FileBrowserComponent::canSelectFiles,
                        [this, chooser] (const juce::FileChooser& fc) {
                            auto f = fc.getResult();
                            if (f.existsAsFile())
                                stageManager_.loadSet (f);
                        });
                });
            });

            // New empty set — dirty-check first
            stageWindow_->setOnNewSet ([this] {
                executeSafeSetOperation ([this] { stageManager_.newSet(); });
            });

            // Wire onSetChanged so StageWindow stays in sync
            stageManager_.onSetChanged = [this] {
                if (stageWindow_ != nullptr) stageWindow_->refresh();
            };

            // Restore pin (always-on-top) state
            if (auto* prefs = appProperties_.getUserSettings())
            {
                bool pinned = prefs->getBoolValue ("stageAlwaysOnTop", false);
                stageWindow_->setPinState (pinned);
            }

            // Restore saved bounds (first-time open in this session)
            if (auto* prefs = appProperties_.getUserSettings())
            {
                int w = prefs->getIntValue ("stageWindowW", 0);
                int h = prefs->getIntValue ("stageWindowH", 0);
                if (w > 100 && h > 50)
                {
                    stageWindow_->setBounds (prefs->getIntValue ("stageWindowX", 0),
                                             prefs->getIntValue ("stageWindowY", 0),
                                             w, h);
                }
            }
        }

        stageWindow_->setVisible (true);
        stageWindow_->toFront (true);
    }
    else
    {
        if (stageWindow_ != nullptr)
            stageWindow_->setVisible (false);
    }

    if (mc_ != nullptr) mc_->setStageWindowVisible (show);
}

// ── Stage: safe operation helper ──────────────────────────────────────────

void UIManager::executeSafeSetOperation (std::function<void()> action)
{
    if (! stageManager_.isDirty())
    {
        action();
        return;
    }

    juce::NativeMessageBox::showYesNoCancelBox (
        juce::MessageBoxIconType::QuestionIcon,
        LvhStr ("STR_UNSAVED_TITLE"),
        LvhStr ("STR_UNSAVED_MSG"),
        nullptr,
        juce::ModalCallbackFunction::create (
            [this, action] (int result)
            {
                if (result == 0) return; // Cancel

                if (result == 1) // Yes — save, then run action
                {
                    auto curFile = stageManager_.getCurrentFile();
                    auto chooser = std::make_shared<juce::FileChooser> (
                        LvhStr ("STR_SAVE_SET_DIALOG"),
                        curFile.existsAsFile()
                            ? curFile   // pre-fill filename for easy overwrite
                            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                        "*.stg");
                    chooser->launchAsync (
                        juce::FileBrowserComponent::saveMode |
                        juce::FileBrowserComponent::canSelectFiles |
                        juce::FileBrowserComponent::warnAboutOverwriting,
                        [this, chooser, action] (const juce::FileChooser& fc)
                        {
                            auto f = fc.getResult();
                            if (f.getFullPathName().isNotEmpty())
                                stageManager_.saveSet (f.withFileExtension ("stg"));
                            action();
                        });
                    return;
                }

                action(); // No — discard and proceed
            }));
}

// ── Stage: window state restore ───────────────────────────────────────────

void UIManager::restoreStageWindow()
{
    if (auto* prefs = appProperties_.getUserSettings())
    {
        if (prefs->getBoolValue ("stageWindowVisible", false))
            toggleStageWindow (true);
    }
}

// ── Settings window ───────────────────────────────────────────────────────

void UIManager::openSettings()
{
    if (settingsWindow_ == nullptr)
    {
        SettingsWindow::Callbacks cbs;
        cbs.onShowLevelMeter = [this] (bool v) {
            if (mc_ != nullptr) mc_->setLevelMeterVisible (v);
        };
        cbs.onShowMidiMonitor = [this] (bool v) {
            if (mc_ != nullptr) mc_->setMidiMonitorVisible (v);
        };
        cbs.onShowInfoMonitor = [this] (bool v) {
            if (mc_ != nullptr) mc_->setMonitorPanelVisible (v);
        };
        cbs.onTransposeChange = [this] (int v) {
            audioEngine_.setTranspose (v);
            if (mc_ != nullptr) mc_->getMonitorPanel().setTransposeDisplay (v);
        };
        cbs.onChannelFilterChange = [this] (int v) {
            audioEngine_.setChannelFilter (v);
        };
        cbs.onPluginPathsChanged = [this] {
            if (onStartPluginScan) onStartPluginScan();
        };
        cbs.onLanguageChanged = [this] (juce::String /*lang*/) {
            refreshAllWindows();
        };
        cbs.onMetronomeClickTypeChanged = [this] (int v) {
            audioEngine_.setMetronomeClickType (
                v == 1 ? MetronomeProcessor::ClickType::Techno
                       : MetronomeProcessor::ClickType::Normal);
        };
        settingsWindow_ = std::make_unique<SettingsWindow> (
            deviceManager_, appProperties_.getUserSettings(), cbs);
    }

    settingsWindow_->setVisible (true);
    settingsWindow_->toFront (true);
}

// ── Language refresh ──────────────────────────────────────────────────────

void UIManager::refreshAllWindows()
{
    if (settingsWindow_ != nullptr) settingsWindow_->refresh();
    if (stageWindow_    != nullptr) stageWindow_   ->refreshLanguage();
    if (mixerWindow_    != nullptr) mixerWindow_   ->refresh();
}

// ── MIDI remote control ───────────────────────────────────────────────────

void UIManager::handleMidiRemote (const juce::MidiMessage& msg)
{
    auto* prefs = appProperties_.getUserSettings();
    if (prefs == nullptr) return;

    // ── Mixer MIDI Learn capture ──────────────────────────────────────────
    if (learnState_.active && msg.isController())
    {
        int cc = msg.getControllerNumber();
        juce::String key = learnState_.bridge != nullptr
                           ? learnState_.bridge->getPluginPath()
                           : "__MASTER__";
        auto& m = mixerMappings_[key];
        switch (learnState_.param)
        {
            case MixerParam::Fader: m.ccFader = cc; break;
            case MixerParam::Pan:   m.ccPan   = cc; break;
            case MixerParam::Mute:  m.ccMute  = cc; break;
            case MixerParam::Solo:  m.ccSolo  = cc; break;
        }
        saveMixerMappings();
        if (mixerWindow_ != nullptr)
            mixerWindow_->setStripLearnMode (learnState_.bridge, learnState_.param, false);
        learnState_.active = false;
        return;
    }

    // ── Mixer CC apply ────────────────────────────────────────────────────
    if (msg.isController() && mixerWindow_ != nullptr)
    {
        int cc = msg.getControllerNumber();
        float norm = (float) msg.getControllerValue() / 127.0f;

        for (auto& [path, map] : mixerMappings_)
        {
            // ── Master strip (no BridgeInstance) ─────────────────────────
            if (path == "__MASTER__")
            {
                if (map.ccFader >= 0 && cc == map.ccFader)
                {
                    setMasterVolume ((double) norm);
                    mixerWindow_->applyMidiValue (nullptr, MixerParam::Fader, norm);
                }
                if (map.ccPan >= 0 && cc == map.ccPan)
                {
                    float pan = norm * 2.0f - 1.0f;
                    mixerWindow_->applyMidiValue (nullptr, MixerParam::Pan, pan);
                }
                continue;
            }

            // ── Instrument / FX strip ─────────────────────────────────────
            auto* bridge = findBridgeByPath (path);
            if (bridge == nullptr) continue;

            if (map.ccFader >= 0 && cc == map.ccFader)
            {
                float gain = norm * (bridge->getRole() == BridgeInstance::Role::Instrument ? 1.5f : 1.0f);
                bridge->mixerGain.store (gain, std::memory_order_relaxed);
                mixerWindow_->applyMidiValue (bridge, MixerParam::Fader, gain);
            }
            if (map.ccPan >= 0 && cc == map.ccPan)
            {
                float pan = norm * 2.0f - 1.0f;
                bridge->mixerPan.store (pan, std::memory_order_relaxed);
                mixerWindow_->applyMidiValue (bridge, MixerParam::Pan, pan);
            }
            if (map.ccMute >= 0 && cc == map.ccMute && msg.getControllerValue() >= 64)
            {
                bool muted = ! bridge->mixerMuted.load();
                bridge->mixerMuted.store (muted, std::memory_order_relaxed);
                mixerWindow_->applyMidiValue (bridge, MixerParam::Mute, muted ? 1.f : 0.f);
            }
            if (map.ccSolo >= 0 && cc == map.ccSolo && msg.getControllerValue() >= 64)
            {
                bool soloed = ! bridge->mixerSoloed.load();
                bridge->mixerSoloed.store (soloed, std::memory_order_relaxed);
                mixerWindow_->applyMidiValue (bridge, MixerParam::Solo, soloed ? 1.f : 0.f);
            }
        }
    }

    // ── Stage remote ─────────────────────────────────────────────────────
    if (stageWindow_ == nullptr) return;

    int method = prefs->getIntValue ("stageRemoteMethod", 0);
    if (method == 0) return; // None — disabled

    // Channel filter (0 = Any)
    int remoteChannel = prefs->getIntValue ("stageRemoteChannel", 0);
    if (remoteChannel != 0 && msg.getChannel() != remoteChannel) return;

    // Program Change → navigate to absolute index
    if (method == 1 && msg.isProgramChange())
    {
        stageWindow_->remoteNavigateTo (msg.getProgramChangeNumber());
        return;
    }

    // Control Change → per-CC-number action (only fire on value >= 64 to
    // avoid double-trigger from footswitch release)
    if (method == 2 && msg.isController() && msg.getControllerValue() >= 64)
    {
        int cc   = msg.getControllerNumber();
        int prev = prefs->getIntValue ("stageRemoteCCPrev", 21);
        int next = prefs->getIntValue ("stageRemoteCCNext", 22);
        int load = prefs->getIntValue ("stageRemoteCCLoad", 23);

        if      (cc == prev) stageWindow_->remoteMoveSelection (-1);
        else if (cc == next) stageWindow_->remoteMoveSelection (+1);
        else if (cc == load) stageWindow_->remoteLoad();
    }
}

// ── Mixer MIDI Learn / mapping helpers ───────────────────────────────────

void UIManager::startMidiLearn (BridgeInstance* b, MixerParam p)
{
    // Cancel any previously active learn session first
    if (learnState_.active && mixerWindow_ != nullptr)
        mixerWindow_->setStripLearnMode (learnState_.bridge, learnState_.param, false);

    learnState_ = { b, p, true };
    if (mixerWindow_ != nullptr)
        mixerWindow_->setStripLearnMode (b, p, true);
}

void UIManager::clearMidiMapping (BridgeInstance* b, MixerParam p)
{
    juce::String key = (b != nullptr) ? b->getPluginPath() : "__MASTER__";
    auto it = mixerMappings_.find (key);
    if (it == mixerMappings_.end()) return;
    switch (p)
    {
        case MixerParam::Fader: it->second.ccFader = -1; break;
        case MixerParam::Pan:   it->second.ccPan   = -1; break;
        case MixerParam::Mute:  it->second.ccMute  = -1; break;
        case MixerParam::Solo:  it->second.ccSolo  = -1; break;
    }
    saveMixerMappings();
}

void UIManager::saveMixerMappings()
{
    auto* prefs = appProperties_.getUserSettings();
    if (prefs == nullptr) return;

    juce::Array<juce::var> arr;
    for (auto& [path, m] : mixerMappings_)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("path",    path);
        obj->setProperty ("ccFader", m.ccFader);
        obj->setProperty ("ccPan",   m.ccPan);
        obj->setProperty ("ccMute",  m.ccMute);
        obj->setProperty ("ccSolo",  m.ccSolo);
        arr.add (juce::var (obj));
    }
    prefs->setValue ("mixerMidiMappings", juce::JSON::toString (arr));
}

void UIManager::loadMixerMappings()
{
    auto* prefs = appProperties_.getUserSettings();
    if (prefs == nullptr) return;

    auto parsed = juce::JSON::parse (prefs->getValue ("mixerMidiMappings", "[]"));
    if (auto* arr = parsed.getArray())
    {
        for (auto& item : *arr)
        {
            if (auto* obj = item.getDynamicObject())
            {
                juce::String path = obj->getProperty ("path").toString();
                if (path.isEmpty()) continue;
                MixerMidiMapping m;
                m.ccFader = (int) obj->getProperty ("ccFader");
                m.ccPan   = (int) obj->getProperty ("ccPan");
                m.ccMute  = (int) obj->getProperty ("ccMute");
                m.ccSolo  = (int) obj->getProperty ("ccSolo");
                mixerMappings_[path] = m;
            }
        }
    }
}

BridgeInstance* UIManager::findBridgeByPath (const juce::String& path) const
{
    for (auto* b : bridgeManager_.getBridges())
        if (b->getPluginPath() == path)
            return b;
    return nullptr;
}

// ── Bridge file choosers ──────────────────────────────────────────────────

juce::File UIManager::getBridgeStartDir() const
{
    juce::File startDir;
    if (auto* prefs = appProperties_.getUserSettings())
        if (prefs->getBoolValue ("rememberLastFolder", true))
        {
            juce::String last = prefs->getValue ("lastBridgeFolder");
            if (last.isNotEmpty()) startDir = juce::File (last);
        }
    if (! startDir.isDirectory())
        startDir = juce::File ("C:/Program Files/Common Files/VST3");
    if (! startDir.isDirectory())
        startDir = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
    return startDir;
}

void UIManager::launchBridgeFileChooser (BridgeInstance::Role role)
{
    juce::String title = (role == BridgeInstance::Role::Effect)
                         ? "Select a VST3 effect plugin to bridge..."
                         : "Select a VST3 plugin to bridge...";

    auto chooser = std::make_shared<juce::FileChooser> (title, getBridgeStartDir(), "*.vst3");
    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, role] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.existsAsFile())
                bridgeManager_.launchBridgeWithPath (f, role);
        });
}

// ── Plugin picker popup (used by Mixer + slots) ───────────────────────────

void UIManager::showPluginPicker (BridgeInstance::Role fixedRole, BridgeInstance* parentInstrument)
{
    juce::Array<juce::PluginDescription> filteredTypes;
    for (auto& t : knownPlugins_.getTypes())
    {
        if (fixedRole == BridgeInstance::Role::Instrument && ! t.isInstrument) continue;
        if (fixedRole == BridgeInstance::Role::Effect && t.isInstrument) continue;
        filteredTypes.add (t);
    }

    juce::PopupMenu m;
    if (filteredTypes.isEmpty())
    {
        m.addItem (1, "(No compatible plugins found)", false, false);
    }
    else
    {
        int id = 100;
        for (auto& t : filteredTypes)
            m.addItem (id++, t.name);
        m.addSeparator();
    }
    m.addItem (1, "Refresh Plugin List...");

    juce::String parentPath = parentInstrument ? parentInstrument->getPluginPath() : juce::String{};

    m.showMenuAsync (juce::PopupMenu::Options(),
        [this, filteredTypes, fixedRole, parentPath] (int result)
        {
            if (result == 1)
            {
                if (onStartPluginScan) onStartPluginScan();
                return;
            }
            int idx = result - 100;
            if (idx >= 0 && idx < filteredTypes.size())
            {
                auto& desc = filteredTypes[idx];
                juce::File pluginFile (desc.fileOrIdentifier);
                if (pluginFile.exists())
                    bridgeManager_.launchBridgeWithPath (pluginFile, fixedRole, {}, std::nullopt, {}, parentPath);
                else if (mc_ != nullptr)
                    mc_->pushSystemMessage ("Plugin not found: " + desc.fileOrIdentifier);
            }
        });
}

// ── Main popup menu ───────────────────────────────────────────────────────

void UIManager::showMainMenu()
{
    juce::PopupMenu m;

    // ── Select Instruments submenu (IDs 3000-3998 = plugins, 3 = refresh) ──
    juce::PopupMenu instrSub;
    juce::Array<juce::PluginDescription> instrumentTypes;
    for (auto& t : knownPlugins_.getTypes())
        if (t.isInstrument) instrumentTypes.add (t);

    if (instrumentTypes.isEmpty())
    {
        instrSub.addItem (3000, "(No instruments found)", false, false);
    }
    else
    {
        int id = 3000;
        for (auto& t : instrumentTypes)
            instrSub.addItem (id++, t.name);
        instrSub.addSeparator();
    }
    instrSub.addItem (3, "Refresh Plugin List...");
    m.addSubMenu ("Select Instruments", instrSub);
    m.addSeparator();

    // ── MIDI Input submenu (IDs 1000-1999) ──
    juce::PopupMenu midiSub;
    auto midiInputs = juce::MidiInput::getAvailableDevices();
    if (midiInputs.isEmpty())
    {
        midiSub.addItem (1000, "No MIDI Device", false, false);
    }
    else
    {
        int id = 1000;
        for (auto& d : midiInputs)
            midiSub.addItem (id++, d.name, true,
                             deviceManager_.isMidiInputDeviceEnabled (d.identifier));
    }
    m.addSubMenu ("MIDI Input", midiSub);

    // ── MIDI Route submenu (ID 4000 = All, 4001-4099 = individual bridge) ──
    juce::PopupMenu routeSub;
    bool allMode = midiRouter_.isRouteToAll();
    routeSub.addItem (4000, "All Bridges", true, allMode);
    if (! bridgeManager_.getBridges().isEmpty())
    {
        routeSub.addSeparator();
        int rid = 4001;
        for (auto* b : bridgeManager_.getBridges())
        {
            juce::String label = juce::File (b->getPluginPath()).getFileNameWithoutExtension();
            routeSub.addItem (rid++, label, true,
                              ! allMode && midiRouter_.getTarget() == b);
        }
    }
    m.addSubMenu ("MIDI Route", routeSub);
    m.addSeparator();
    m.addItem (1, "Settings...");
    m.addSeparator();
    m.addItem (6001, "Open Stage Set");
    m.addSeparator();
    m.addItem (5001, "Save Project...");
    m.addItem (5002, "Open Project...");

    // ── Recent bridge files (IDs 2000-2019) ──
    auto recents = bridgeManager_.getRecentBridgeFiles();
    if (recents.size() > 0)
    {
        m.addSeparator();
        int id = 2000;
        for (auto& f : recents)
            m.addItem (id++, f.getFileNameWithoutExtension());
    }

    // Capture bridge snapshot for route selection
    // (BridgeInstance* pointers remain valid until onDisconnected on message thread)
    struct BridgeEntry { BridgeInstance* ptr; };
    juce::Array<BridgeEntry> bridgeSnapshot;
    for (auto* b : bridgeManager_.getBridges()) bridgeSnapshot.add ({ b });

    m.showMenuAsync (juce::PopupMenu::Options(),
        [this, midiInputs, recents, instrumentTypes, bridgeSnapshot] (int result)
        {
            if (result == 1)
            {
                openSettings();
            }
            else if (result == 6001)
            {
                bool nowVisible = stageWindow_ == nullptr || ! stageWindow_->isVisible();
                toggleStageWindow (nowVisible);
            }
            else if (result == 5001)
            {
                auto curFile = projectSerializer_.getCurrentProjectFile();
                auto chooser = std::make_shared<juce::FileChooser> (
                    "Save Project...",
                    curFile.existsAsFile()
                        ? curFile.getParentDirectory()
                        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                    "*.lvh");
                chooser->launchAsync (
                    juce::FileBrowserComponent::saveMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::warnAboutOverwriting,
                    [this, chooser] (const juce::FileChooser& fc) {
                        auto f = fc.getResult();
                        if (f.getFullPathName().isNotEmpty())
                        {
                            auto lvhFile = f.withFileExtension ("lvh");
                            projectSerializer_.setCurrentProjectFile (lvhFile);
                            projectSerializer_.saveProject (lvhFile);
                        }
                    });
            }
            else if (result == 5002)
            {
                auto curFile = projectSerializer_.getCurrentProjectFile();
                auto chooser = std::make_shared<juce::FileChooser> (
                    "Open Project...",
                    curFile.existsAsFile()
                        ? curFile.getParentDirectory()
                        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                    "*.lvh");
                chooser->launchAsync (
                    juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectFiles,
                    [this, chooser] (const juce::FileChooser& fc) {
                        auto f = fc.getResult();
                        if (f.existsAsFile())
                        {
                            projectSerializer_.setCurrentProjectFile (f);
                            projectSerializer_.loadProject (f);
                        }
                    });
            }
            else if (result == 3)
            {
                if (onStartPluginScan) onStartPluginScan();
            }
            else if (result >= 1000 && result < 2000)
            {
                int idx = result - 1000;
                if (idx < midiInputs.size())
                {
                    auto& d = midiInputs[idx];
                    bool wasEnabled = deviceManager_.isMidiInputDeviceEnabled (d.identifier);
                    for (auto& dev : midiInputs)
                        deviceManager_.setMidiInputDeviceEnabled (dev.identifier, false);
                    if (! wasEnabled)
                        deviceManager_.setMidiInputDeviceEnabled (d.identifier, true);
                    updateMidiDeviceLabel();
                }
            }
            else if (result >= 2000 && result < 3000)
            {
                int idx = result - 2000;
                if (idx < recents.size())
                    bridgeManager_.launchBridgeWithPath (recents[idx]);
            }
            else if (result >= 3000 && result < 3999)
            {
                int idx = result - 3000;
                if (idx < instrumentTypes.size())
                {
                    auto& desc = instrumentTypes[idx];
                    juce::File pluginFile (desc.fileOrIdentifier);
                    auto role = BridgeInstance::Role::Instrument;
                    if (pluginFile.exists())
                        bridgeManager_.launchBridgeWithPath (pluginFile, role);
                    else if (mc_ != nullptr)
                        mc_->pushSystemMessage ("Plugin not found: " + desc.fileOrIdentifier);
                }
            }
            else if (result == 4000)
            {
                midiRouter_.setRouteToAll();
                if (mc_ != nullptr) mc_->pushSystemMessage ("MIDI Route: All Bridges");
            }
            else if (result >= 4001 && result < 4100)
            {
                int idx = result - 4001;
                if (idx < bridgeSnapshot.size())
                {
                    auto* target = bridgeSnapshot[idx].ptr;
                    midiRouter_.setRouteToTarget (target);
                    if (mc_ != nullptr)
                    {
                        juce::String name = juce::File (target->getPluginPath())
                                                .getFileNameWithoutExtension();
                        mc_->pushSystemMessage ("MIDI Route: " + name + " only");
                    }
                }
            }
        });
}
