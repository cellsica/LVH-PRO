#include "UIManager.h"
#include "MainComponent.h"
#include "MixerWindow.h"
#include "SettingsWindow.h"

UIManager::UIManager (AudioEngine&                  audioEngine,
                       BridgeManager&                bridgeManager,
                       ProjectSerializer&            projectSerializer,
                       MidiRoutingManager&           midiRouter,
                       juce::AudioDeviceManager&     deviceManager,
                       juce::ApplicationProperties&  appProperties,
                       juce::KnownPluginList&        knownPlugins)
    : audioEngine_      (audioEngine),
      bridgeManager_    (bridgeManager),
      projectSerializer_(projectSerializer),
      midiRouter_       (midiRouter),
      deviceManager_    (deviceManager),
      appProperties_    (appProperties),
      knownPlugins_     (knownPlugins)
{}

UIManager::~UIManager() = default;

// ── Lifecycle ─────────────────────────────────────────────────────────────

void UIManager::setMainComponent (MainComponent* mc)
{
    mc_ = mc;
    if (mc == nullptr) return;

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

    mc_->onLogoRightClick = [this] { showMainMenu(); };
    mc_->onMixerToggle    = [this] (bool show) { toggleMixerWindow (show); };
    mc_->onLaunchBridgeClicked = [this] { launchBridgeFileChooser(); };
}

void UIManager::shutdown()
{
    settingsWindow_.reset();
    mixerWindow_.reset();
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
                auto bounds = b->getWindowBounds();
                if (bounds.getWidth() > 0 && bounds.getHeight() > 0)
                    b->sendWindowPos (bounds.getX(), bounds.getY(),
                                      bounds.getWidth(), bounds.getHeight());
            };
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
        settingsWindow_ = std::make_unique<SettingsWindow> (
            deviceManager_, appProperties_.getUserSettings(), cbs);
    }

    // Update plugin info page
    if (auto* slot = audioEngine_.getSlot())
        if (slot->isLoaded())
            if (auto* proc = slot->getProcessor())
                settingsWindow_->updatePluginInfo (
                    proc->getName(),
                    proc->getLatencySamples(),
                    proc->getPluginDescription().pluginFormatName,
                    proc->getTotalNumInputChannels(),
                    proc->getTotalNumOutputChannels());

    settingsWindow_->setVisible (true);
    settingsWindow_->toFront (true);
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

// ── Main popup menu ───────────────────────────────────────────────────────

void UIManager::showMainMenu()
{
    juce::PopupMenu m;

    // ── Select Instruments submenu (IDs 3000-3998 = plugins, 3 = refresh) ──
    juce::PopupMenu instrSub;
    auto pluginTypes = knownPlugins_.getTypes();
    if (pluginTypes.isEmpty())
    {
        instrSub.addItem (3000, "(No plugins scanned yet)", false, false);
    }
    else
    {
        int id = 3000;
        for (auto& t : pluginTypes)
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
    m.addItem (5001, "Save Project...");
    m.addItem (5002, "Open Project...");
    m.addSeparator();
    m.addItem (2, "[Pro] Launch Bridge...");
    m.addItem (5003, "[Pro] Launch Bridge as Effect...");

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
        [this, midiInputs, recents, pluginTypes, bridgeSnapshot] (int result)
        {
            if (result == 1)
            {
                openSettings();
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
            else if (result == 2)
            {
                launchBridgeFileChooser();
            }
            else if (result == 5003)
            {
                launchBridgeFileChooser (BridgeInstance::Role::Effect);
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
                if (idx < pluginTypes.size())
                {
                    juce::File pluginFile (pluginTypes[idx].fileOrIdentifier);
                    if (pluginFile.exists())
                        bridgeManager_.launchBridgeWithPath (pluginFile);
                    else if (mc_ != nullptr)
                        mc_->pushSystemMessage ("Plugin not found: "
                                               + pluginTypes[idx].fileOrIdentifier);
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
