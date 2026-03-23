#include "MainComponent.h"
#include "SettingsWindow.h"
#include "AudioEngine.h"
#include "PluginScanThread.h"
#include "PluginSlot.h"
#include "BridgeInstance.h"
#include "MidiRoutingManager.h"
#include "ProjectSerializer.h"
#include "BridgeManager.h"

// =====================================================================
// Main Application
// =====================================================================
class LvhProApplication : public JUCEApplication,
                                public MidiInputCallback,
                                public MidiKeyboardState::Listener
{
public:
    LvhProApplication() {}

    const String getApplicationName() override    { return "LVH-PRO"; }
    const String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override    { return true; }

    // Hardware MIDI input
    void handleIncomingMidiMessage (MidiInput*, const MidiMessage& message) override
    {
        keyboardState.processNextMidiEvent (message);
        // Forward to selected Bridge(s) via IPC.
        midiRouter.sendMidi (message);
        auto msg = message;
        MessageManager::callAsync ([this, msg] {
            if (auto* mc = mainComp()) mc->getMonitorPanel().pushMidiMessage (msg);
        });
    }

    // MidiKeyboardState::Listener — note events from PC keyboard / on-screen keyboard
    void handleNoteOn (MidiKeyboardState*, int channel, int note, float velocity) override
    {
        // Called on the audio thread — capture only POD values, build Strings on the message thread.
        MessageManager::callAsync ([this, channel, note, velocity] {
            String name = MidiMessage::getMidiNoteName (note, true, true, 3);
            String text = "Note: " + String (note) + " (" + name + ")"
                        + " Vel: " + String (roundToInt (velocity * 127.f))
                        + " Ch: "  + String (channel);
            auto msg = MidiMessage::noteOn (channel, note, velocity);
            midiRouter.sendMidi (msg);
            if (auto* mc = mainComp())
            {
                mc->setMidiMonitorText (text);
                mc->getMonitorPanel().pushMidiMessage (msg);
            }
        });
    }
    void handleNoteOff (MidiKeyboardState*, int channel, int note, float velocity) override
    {
        // Called on the audio thread — defer to message thread.
        MessageManager::callAsync ([this, channel, note, velocity] {
            auto msg = MidiMessage::noteOff (channel, note, velocity);
            midiRouter.sendMidi (msg);
            if (auto* mc = mainComp())
                mc->getMonitorPanel().pushMidiMessage (msg);
        });
    }

    void initialise (const String&) override
    {
        // Initialize settings persistence
        PropertiesFile::Options opts;
        opts.applicationName       = "LVH-PRO";
        opts.filenameSuffix        = "settings";
        opts.folderName            = "cellsica";
        opts.osxLibrarySubFolder   = "Application Support";
        appProperties.setStorageParameters (opts);

        deviceManager.initialiseWithDefaultDevices (0, 2);
        audioEngine.initialise (deviceManager);
        audioEngine.buildGraphWithSineWave();

        mainWindow.reset (new MainWindow (getApplicationName(), keyboardState));

        pcKeyListener = std::make_unique<PCKeyboardListener> (keyboardState);
        pcKeyListener->onOctaveShift = [this] (int delta) { midiRouter.applyOctaveShift (delta); };
        mainWindow->addKeyListener (pcKeyListener.get());

        keyboardState.addListener (this);

        wireUICallbacks();
        wireBridgeManagerCallbacks();
        wireSerializerCallbacks();

        // Apply saved settings
        if (auto* prefs = appProperties.getUserSettings())
        {
            if (auto* mc = mainComp())
            {
                mc->setLevelMeterVisible  (prefs->getBoolValue ("showLevelMeter",  true));
                mc->setMidiMonitorVisible (prefs->getBoolValue ("showMidiMonitor", true));
                if (! prefs->getBoolValue ("showInfoMonitor", true))
                    mc->setMonitorPanelVisible (false);
            }
            int savedTranspose = prefs->getIntValue ("transpose", 0);
            int savedChannel   = prefs->getIntValue ("channelFilter", 0);
            if (savedTranspose != 0) audioEngine.setTranspose (savedTranspose);
            if (savedChannel   != 0) audioEngine.setChannelFilter (savedChannel);
        }

        deviceManager.addMidiInputDeviceCallback (String(), this);
        deviceManager.addMidiInputDeviceCallback (String(), &audioEngine.getPlayer());

        if (loadPluginCache())
        {
            if (auto* mc = mainComp())
                mainWindow->setName (getApplicationName() + "  \xe2\x80\x94  "
                                     + String (knownPlugins.getNumTypes()) + " plugins (cached). Ready.");
        }
        else
        {
            MessageManager::callAsync ([this] { startPluginScan(); });
        }
    }

    void shutdown() override
    {
        settingsWindow.reset();
        keyboardState.removeListener (this);

        if (scanToken) scanToken->store (false);
        scanThread.reset();

        if (mainWindow) mainWindow->removeKeyListener (pcKeyListener.get());
        pcKeyListener.reset();

        // Switch the audio graph back to sine wave so BridgeSyncProcessors are
        // removed from the graph before we destroy the BridgeInstances (which
        // own the SHM/SyncEvents those processors reference).
        audioEngine.buildGraphWithSineWave();
        bridgeManager_.clearBridges(); // calls BridgeInstance::shutdown() on each bridge

        deviceManager.removeMidiInputDeviceCallback (String(), this);
        deviceManager.removeMidiInputDeviceCallback (String(), &audioEngine.getPlayer());
        audioEngine.shutdown (deviceManager);
        mainWindow.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MixerWindow> mixerWindow;

    void toggleMixerWindow (bool show)
    {
        if (show)
        {
            if (mixerWindow == nullptr)
            {
                mixerWindow = std::make_unique<MixerWindow> ("Mixer Console");
                mixerWindow->onClose = [this] {
                    if (auto* mc = mainComp()) mc->setMixerWindowVisible (false);
                    mixerWindow->setVisible (false);
                };
                mixerWindow->onMasterGainChange = [this] (float v) {
                    masterVolume = (double) v;
                    audioEngine.setOutputGain (v);
                    if (auto* mc = mainComp())
                    {
                        mc->getVolumeSlider().setValue ((double) v, dontSendNotification);
                        mc->setVolumeDisplay ((double) v);
                    }
                };
                mixerWindow->onToggleFxWindow = [this] (BridgeInstance* b) {
                    // Bring the FX plugin window to front by re-sending its last known bounds.
                    auto bounds = b->getWindowBounds();
                    if (bounds.getWidth() > 0 && bounds.getHeight() > 0)
                        b->sendWindowPos (bounds.getX(), bounds.getY(),
                                          bounds.getWidth(), bounds.getHeight());
                };
            }

            // Sync MASTER fader to current Core slider value
            if (auto* mc = mainComp())
                mixerWindow->setMasterGain ((float) mc->getVolumeSlider().getValue());

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
            mixerWindow->updateBridges (instruments, effects);

            mixerWindow->setVisible (true);
            mixerWindow->toFront (true);
        }
        else
        {
            if (mixerWindow != nullptr)
                mixerWindow->setVisible (false);
        }
    }

    File getCacheFile() const
    {
        return File::getSpecialLocation (File::userApplicationDataDirectory)
                   .getChildFile ("cellsica/LVH-PRO/KnownPlugins.xml");
    }

    File getDeadMansPedalFile() const
    {
        return getCacheFile().getParentDirectory().getChildFile ("DeadPlugins.txt");
    }

    void savePluginCache()
    {
        if (knownPlugins.getNumTypes() == 0) return;
        auto cacheFile = getCacheFile();
        cacheFile.getParentDirectory().createDirectory();
        if (auto xml = knownPlugins.createXml()) xml->writeTo (cacheFile);
    }

    bool loadPluginCache()
    {
        auto cacheFile = getCacheFile();
        if (! cacheFile.existsAsFile()) return false;
        if (auto xml = XmlDocument::parse (cacheFile))
        {
            knownPlugins.recreateFromXml (*xml);
            if (knownPlugins.getNumTypes() > 0)
            {
                knownPlugins.sort (KnownPluginList::sortAlphabetically, true);
                return true;
            }
        }
        return false;
    }

    void startPluginScan()
    {
        if (scanToken) scanToken->store (false);
        auto token = std::make_shared<std::atomic<bool>> (true);
        scanToken  = token;
        scanThread.reset();
        knownPlugins.clear();

        if (auto* mc = mainComp()) mc->showScanOverlay();

        StringArray extraPaths;
        if (auto* prefs = appProperties.getUserSettings())
            extraPaths = StringArray::fromTokens (prefs->getValue ("pluginScanPaths"), "|", "");

        scanThread = std::make_unique<PluginScanThread> (
            knownPlugins, getDeadMansPedalFile(), extraPaths,
            [this, token] (const String& fn) {
                if (! token->load()) return;
                if (auto* mc = mainComp()) mc->updateScanProgress (fn);
            },
            [this, token] {
                if (! token->load()) return;
                knownPlugins.sort (KnownPluginList::sortAlphabetically, true);
                savePluginCache();
                if (auto* mc = mainComp())
                {
                    mc->hideScanOverlay();
                    String msg = knownPlugins.getNumTypes() == 0
                                 ? "No Plugins Found."
                                 : String (knownPlugins.getNumTypes()) + " plugins. Ready.";
                    mainWindow->setName (getApplicationName() + "  \xe2\x80\x94  " + msg);
                }
            });
        scanThread->startThread();
    }

    void openSettings()
    {
        if (settingsWindow == nullptr)
        {
            SettingsWindow::Callbacks cbs;
            cbs.onShowLevelMeter = [this] (bool v) {
                if (auto* mc = mainComp()) mc->setLevelMeterVisible (v);
            };
            cbs.onShowMidiMonitor = [this] (bool v) {
                if (auto* mc = mainComp()) mc->setMidiMonitorVisible (v);
            };
            cbs.onShowInfoMonitor = [this] (bool v) {
                if (auto* mc = mainComp()) mc->setMonitorPanelVisible (v);
            };
            cbs.onTransposeChange = [this] (int v) {
                audioEngine.setTranspose (v);
                if (auto* mc = mainComp()) mc->getMonitorPanel().setTransposeDisplay (v);
            };
            cbs.onChannelFilterChange = [this] (int v) {
                audioEngine.setChannelFilter (v);
            };
            cbs.onPluginPathsChanged = [this] {
                startPluginScan();
            };
            settingsWindow = std::make_unique<SettingsWindow> (
                deviceManager, appProperties.getUserSettings(), cbs);
        }

        // Update plugin info page
        if (auto* slot = audioEngine.getSlot())
            if (slot->isLoaded())
                if (auto* proc = slot->getProcessor())
                    settingsWindow->updatePluginInfo (
                        proc->getName(),
                        proc->getLatencySamples(),
                        proc->getPluginDescription().pluginFormatName,
                        proc->getTotalNumInputChannels(),
                        proc->getTotalNumOutputChannels());

        settingsWindow->setVisible (true);
        settingsWindow->toFront (true);
    }

    void wireBridgeManagerCallbacks()
    {
        bridgeManager_.onMessage = [this] (const juce::String& msg) {
            if (auto* mc = mainComp()) mc->pushSystemMessage (msg);
        };

        bridgeManager_.onGraphRebuilt = [this] (juce::Array<BridgeInstance*> instruments,
                                                  juce::Array<BridgeInstance*> effects) {
            if (mixerWindow != nullptr)
                mixerWindow->updateBridges (instruments, effects);
        };

        bridgeManager_.onBridgeDisconnectedMidi = [this] (BridgeInstance* b) {
            if (midiRouter.handleBridgeDisconnected (b))
                if (auto* mc = mainComp())
                    mc->pushSystemMessage ("MIDI Route reset to: All Bridges");
        };

        bridgeManager_.onApplyPendingMidiTarget = [this] (const juce::String& path,
                                                           BridgeInstance* b) {
            midiRouter.tryApplyPendingTarget (path, b);
        };
    }

    void wireSerializerCallbacks()
    {
        projectSerializer_.onMessage = [this] (const juce::String& msg) {
            if (auto* mc = mainComp()) mc->pushSystemMessage (msg);
        };

        projectSerializer_.onLaunchBridge = [this] (const juce::File& f, BridgeInstance::Role role) {
            // Closure injection: take all pending data here and pass by value to BridgeManager.
            // BridgeManager never needs to call back into ProjectSerializer.
            auto path = f.getFullPathName();
            bridgeManager_.launchBridgeWithPath (
                f, role,
                projectSerializer_.takePendingState  (path),
                projectSerializer_.takePendingMixer  (path),
                projectSerializer_.takePendingBounds (path));
        };

        projectSerializer_.onProjectResetRequired = [this] {
            audioEngine.buildGraphWithSineWave();
            bridgeManager_.clearBridges();
        };

        projectSerializer_.getMasterVolume = [this] () -> double {
            return mainComp() ? mainComp()->getVolumeSlider().getValue() : masterVolume;
        };

        projectSerializer_.getCoreWindowBounds = [this] () -> juce::Rectangle<int> {
            return mainWindow != nullptr ? mainWindow->getBounds() : juce::Rectangle<int>{};
        };

        projectSerializer_.getMixerVisible = [this] () -> bool {
            return mixerWindow != nullptr && mixerWindow->isVisible();
        };

        projectSerializer_.getMixerWindowBounds = [this] () -> juce::Rectangle<int> {
            return mixerWindow != nullptr ? mixerWindow->getBounds() : juce::Rectangle<int>{};
        };

        projectSerializer_.onMasterVolumeChanged = [this] (double vol) {
            masterVolume = vol;
            if (auto* mc = mainComp())
            {
                mc->getVolumeSlider().setValue (vol, juce::dontSendNotification);
                mc->setVolumeDisplay (vol);
                audioEngine.setOutputGain ((float) vol);
                mc->pushSystemMessage ("  masterVol loaded: " + juce::String (vol, 3));
            }
            if (mixerWindow != nullptr) mixerWindow->setMasterGain ((float) vol);
        };

        projectSerializer_.onCoreWindowBoundsChanged = [this] (juce::Rectangle<int> b) {
            if (mainWindow != nullptr) mainWindow->setBounds (b);
        };

        projectSerializer_.onMixerWindowRestored = [this] (bool visible, juce::Rectangle<int> bounds) {
            if (visible)
            {
                toggleMixerWindow (true);
                if (mixerWindow != nullptr && bounds.getWidth() > 100 && bounds.getHeight() > 50)
                    mixerWindow->setBounds (bounds);
                if (auto* mc = mainComp()) mc->setMixerWindowVisible (true);
            }
            else
            {
                if (mixerWindow != nullptr) mixerWindow->setVisible (false);
                if (auto* mc = mainComp()) mc->setMixerWindowVisible (false);
            }
        };
    }

    void wireUICallbacks()
    {
        auto* mc = mainComp();
        if (mc == nullptr) return;

        // Speaker mute button + volume slider (share savedGain)
        auto savedGain = std::make_shared<double> (1.0);

        mc->getSpeakerButton().onClick = [this, mc, savedGain] {
            bool muted = mc->getSpeakerButton().getToggleState();
            if (muted)
            {
                *savedGain = mc->getVolumeSlider().getValue();
                mc->getVolumeSlider().setValue (0.0, sendNotificationSync);
            }
            else
            {
                mc->getVolumeSlider().setValue (*savedGain > 0.0 ? *savedGain : 1.0,
                                                sendNotificationSync);
            }
        };

        mc->onVolumeChanged = [this, mc, savedGain] (double v) {
            masterVolume = v;
            audioEngine.setOutputGain ((float) v);
            if (mixerWindow != nullptr) mixerWindow->setMasterGain ((float) v);
            // If slider is moved away from 0 while muted, auto-unmute
            if (v > 0.0 && mc->getSpeakerButton().getToggleState())
            {
                *savedGain = v;
                mc->getSpeakerButton().setToggleState (false, dontSendNotification);
            }
        };

        // Level meter source
        mc->getLevelMeter().getPeak = [this] (int ch) { return audioEngine.exchangePeak (ch); };

        // Monitor panel: CPU usage source
        mc->getMonitorPanel().getCpuUsage = [this] { return deviceManager.getCpuUsage(); };

        // LVH logo right-click: Instruments / Settings / MIDI / Bridge
        mc->onLogoRightClick = [this] {
            PopupMenu m;

            // ── Select Instruments submenu (IDs 3000-3998 = plugins, 3 = refresh) ──
            PopupMenu instrSub;
            auto pluginTypes = knownPlugins.getTypes();
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
            PopupMenu midiSub;
            auto midiInputs = MidiInput::getAvailableDevices();
            if (midiInputs.isEmpty())
            {
                midiSub.addItem (1000, "No MIDI Device", false, false);
            }
            else
            {
                int id = 1000;
                for (auto& d : midiInputs)
                    midiSub.addItem (id++, d.name, true,
                                     deviceManager.isMidiInputDeviceEnabled (d.identifier));
            }
            m.addSubMenu ("MIDI Input", midiSub);

            // ── MIDI Route submenu (ID 4000 = All, 4001-4099 = individual bridge) ──
            PopupMenu routeSub;
            bool allMode = midiRouter.isRouteToAll();
            routeSub.addItem (4000, "All Bridges", true, allMode);
            if (! bridgeManager_.getBridges().isEmpty())
            {
                routeSub.addSeparator();
                int rid = 4001;
                for (auto* b : bridgeManager_.getBridges())
                {
                    juce::String label = juce::File (b->getPluginPath()).getFileNameWithoutExtension();
                    routeSub.addItem (rid++, label, true,
                                      ! allMode && midiRouter.getTarget() == b);
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

            // Capture bridges snapshot for route selection (pointer + display name).
            // BridgeInstance* pointers remain valid until onDisconnected on message thread.
            struct BridgeEntry { BridgeInstance* ptr; };
            juce::Array<BridgeEntry> bridgeSnapshot;
            for (auto* b : bridgeManager_.getBridges()) bridgeSnapshot.add ({ b });

            m.showMenuAsync (PopupMenu::Options(), [this, midiInputs, recents, pluginTypes, bridgeSnapshot] (int result) {
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
                    if (auto* mc = mainComp()) mc->onLaunchBridgeClicked();
                }
                else if (result == 5003)
                {
                    // Launch a VST3 plugin as an Effect bridge
                    juce::File startDir;
                    if (auto* prefs = appProperties.getUserSettings())
                        if (prefs->getBoolValue ("rememberLastFolder", true))
                        {
                            juce::String last = prefs->getValue ("lastBridgeFolder");
                            if (last.isNotEmpty()) startDir = juce::File (last);
                        }
                    if (! startDir.isDirectory())
                        startDir = juce::File ("C:/Program Files/Common Files/VST3");
                    if (! startDir.isDirectory())
                        startDir = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);

                    auto chooser = std::make_shared<juce::FileChooser> (
                        "Select a VST3 effect plugin to bridge...", startDir, "*.vst3");
                    chooser->launchAsync (
                        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                        [this, chooser] (const juce::FileChooser& fc)
                        {
                            auto f = fc.getResult();
                            if (f.existsAsFile())
                                bridgeManager_.launchBridgeWithPath (f, BridgeInstance::Role::Effect);
                        });
                }
                else if (result == 3)
                {
                    startPluginScan();
                }
                else if (result >= 1000 && result < 2000)
                {
                    int idx = result - 1000;
                    if (idx < midiInputs.size())
                    {
                        auto& d = midiInputs[idx];
                        bool wasEnabled = deviceManager.isMidiInputDeviceEnabled (d.identifier);
                        for (auto& dev : midiInputs)
                            deviceManager.setMidiInputDeviceEnabled (dev.identifier, false);
                        if (! wasEnabled)
                            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
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
                        if (pluginFile.exists())   // existsAsFile() fails for .vst3 bundle dirs
                            bridgeManager_.launchBridgeWithPath (pluginFile);
                        else if (auto* mc = mainComp())
                            mc->pushSystemMessage ("Plugin not found: "
                                                   + pluginTypes[idx].fileOrIdentifier);
                    }
                }
                else if (result == 4000)
                {
                    midiRouter.setRouteToAll();
                    if (auto* mc = mainComp())
                        mc->pushSystemMessage ("MIDI Route: All Bridges");
                }
                else if (result >= 4001 && result < 4100)
                {
                    int idx = result - 4001;
                    if (idx < bridgeSnapshot.size())
                    {
                        auto* target = bridgeSnapshot[idx].ptr;
                        midiRouter.setRouteToTarget (target);
                        if (auto* mc = mainComp())
                        {
                            juce::String name = juce::File (target->getPluginPath())
                                                    .getFileNameWithoutExtension();
                            mc->pushSystemMessage ("MIDI Route: " + name + " only");
                        }
                    }
                }
            });
        };

        mc->onPanicClicked = [this] { audioEngine.allNotesOff(); };

        mc->onOctaveShift = [this] (int delta) { midiRouter.applyOctaveShift (delta); };

        midiRouter.onOctaveChanged = [this] (int newOffset) {
            if (pcKeyListener) pcKeyListener->setOctaveOffset (newOffset);
            if (auto* mc = mainComp())
            {
                mc->getKeyboardComponent().setOctaveOffset (newOffset);
                mc->setOctaveDisplay (4 + newOffset);
            }
        };

        mc->onMixerToggle = [this] (bool show) { toggleMixerWindow (show); };

        mc->onLaunchBridgeClicked = [this] {

            // Determine the start directory:
            //   1. Last opened folder (if "remember" is enabled and stored)
            //   2. Default VST3 system folder
            //   3. Desktop as final fallback
            juce::File startDir;
            if (auto* prefs = appProperties.getUserSettings())
                if (prefs->getBoolValue ("rememberLastFolder", true))
                {
                    juce::String last = prefs->getValue ("lastBridgeFolder");
                    if (last.isNotEmpty()) startDir = juce::File (last);
                }
            if (! startDir.isDirectory())
                startDir = juce::File ("C:/Program Files/Common Files/VST3");
            if (! startDir.isDirectory())
                startDir = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);

            auto chooser = std::make_shared<juce::FileChooser> (
                "Select a VST3 plugin to bridge...", startDir, "*.vst3");

            chooser->launchAsync (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, chooser] (const juce::FileChooser& fc)
                {
                    auto result = fc.getResult();
                    if (result.existsAsFile())
                        bridgeManager_.launchBridgeWithPath (result);
                });
        };
    }

    MainComponent* mainComp()
    {
        return mainWindow != nullptr
            ? dynamic_cast<MainComponent*> (mainWindow->getContentComponent())
            : nullptr;
    }

    class MainWindow : public DocumentWindow
    {
    public:
        MainWindow (String name, MidiKeyboardState& state)
            : DocumentWindow (name, Colour (0xff14141f), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (state), true);
            setResizable (true, true);
            setResizeLimits (600, 400, 3840, 2160);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    double masterVolume = 1.0;  // mirrors volume slider; updated in onValueChange

    MidiKeyboardState keyboardState;
    AudioDeviceManager deviceManager;
    KnownPluginList knownPlugins;
    AudioEngine audioEngine { keyboardState };
    ApplicationProperties appProperties;
    BridgeManager    bridgeManager_  { audioEngine, deviceManager, appProperties };
    MidiRoutingManager midiRouter    { bridgeManager_.getBridges() };           // must be declared after bridgeManager_
    ProjectSerializer  projectSerializer_ { bridgeManager_.getBridges(), audioEngine, midiRouter,
                                            deviceManager, appProperties };  // after midiRouter
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PCKeyboardListener> pcKeyListener;
    std::unique_ptr<PluginScanThread> scanThread;
    std::shared_ptr<std::atomic<bool>> scanToken;
    std::unique_ptr<SettingsWindow> settingsWindow;
};

START_JUCE_APPLICATION (LvhProApplication)
