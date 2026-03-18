#include "MainComponent.h"
#include "SettingsWindow.h"
#include "AudioEngine.h"
#include "PluginScanThread.h"
#include "PluginSlot.h"
#include "BridgeInstance.h"

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

    // MIDI routing helper — called from MIDI thread or message thread.
    // Uses atomic reads: no locking, negligible overhead.
    void sendMidiToBridges (const juce::MidiMessage& msg)
    {
        if (routeToAll.load (std::memory_order_relaxed))
        {
            for (auto* b : bridges) b->sendMidi (msg);
        }
        else
        {
            if (auto* t = midiTargetBridge.load (std::memory_order_relaxed))
                t->sendMidi (msg);
        }
    }

    // Hardware MIDI input
    void handleIncomingMidiMessage (MidiInput*, const MidiMessage& message) override
    {
        keyboardState.processNextMidiEvent (message);
        // Forward to selected Bridge(s) via IPC.
        sendMidiToBridges (message);
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
            String name = MidiMessage::getMidiNoteName (note, true, true, 4);
            String text = "Note: " + String (note) + " (" + name + ")"
                        + " Vel: " + String (roundToInt (velocity * 127.f))
                        + " Ch: "  + String (channel);
            auto msg = MidiMessage::noteOn (channel, note, velocity);
            sendMidiToBridges (msg);
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
            sendMidiToBridges (msg);
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
        mainWindow->addKeyListener (pcKeyListener.get());

        keyboardState.addListener (this);

        wireUICallbacks();

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
        bridges.clear(); // calls BridgeInstance::shutdown() on each bridge

        deviceManager.removeMidiInputDeviceCallback (String(), this);
        deviceManager.removeMidiInputDeviceCallback (String(), &audioEngine.getPlayer());
        audioEngine.shutdown (deviceManager);
        mainWindow.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
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

        scanThread = std::make_unique<PluginScanThread> (
            knownPlugins, getDeadMansPedalFile(),
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

        mc->getVolumeSlider().onValueChange = [this, mc, savedGain] {
            double v = mc->getVolumeSlider().getValue();
            audioEngine.setOutputGain ((float) v);
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
            bool allMode = routeToAll.load();
            routeSub.addItem (4000, "All Bridges", true, allMode);
            if (! bridges.isEmpty())
            {
                routeSub.addSeparator();
                int rid = 4001;
                for (auto* b : bridges)
                {
                    juce::String label = juce::File (b->getPluginPath()).getFileNameWithoutExtension();
                    routeSub.addItem (rid++, label, true,
                                      ! allMode && midiTargetBridge.load() == b);
                }
            }
            m.addSubMenu ("MIDI Route", routeSub);
            m.addSeparator();
            m.addItem (1, "Settings...");
            m.addSeparator();
            m.addItem (2, "[Pro] Launch Bridge...");

            // ── Recent bridge files (IDs 2000-2019) ──
            auto recents = getRecentBridgeFiles();
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
            for (auto* b : bridges) bridgeSnapshot.add ({ b });

            m.showMenuAsync (PopupMenu::Options(), [this, midiInputs, recents, pluginTypes, bridgeSnapshot] (int result) {
                if (result == 1)
                {
                    openSettings();
                }
                else if (result == 2)
                {
                    if (auto* mc = mainComp()) mc->onLaunchBridgeClicked();
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
                        launchBridgeWithPath (recents[idx]);
                }
                else if (result >= 3000 && result < 3999)
                {
                    int idx = result - 3000;
                    if (idx < pluginTypes.size())
                    {
                        juce::File pluginFile (pluginTypes[idx].fileOrIdentifier);
                        if (pluginFile.exists())   // existsAsFile() fails for .vst3 bundle dirs
                            launchBridgeWithPath (pluginFile);
                        else if (auto* mc = mainComp())
                            mc->pushSystemMessage ("Plugin not found: "
                                                   + pluginTypes[idx].fileOrIdentifier);
                    }
                }
                else if (result == 4000)
                {
                    // All Bridges mode
                    midiTargetBridge.store (nullptr);
                    routeToAll.store (true);
                    if (auto* mc = mainComp())
                        mc->pushSystemMessage ("MIDI Route: All Bridges");
                }
                else if (result >= 4001 && result < 4100)
                {
                    int idx = result - 4001;
                    if (idx < bridgeSnapshot.size())
                    {
                        auto* target = bridgeSnapshot[idx].ptr;
                        midiTargetBridge.store (target);
                        routeToAll.store (false);
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
                        launchBridgeWithPath (result);
                });
        };
    }

    // Collect all Connected bridges and rebuild the audio graph accordingly.
    // Falls back to sine wave if no bridges are connected.
    void rebuildBridgeGraph()
    {
        juce::Array<BridgeInstance*> active;
        for (auto* b : bridges)
            if (b->getState() == BridgeInstance::State::Connected)
                active.add (b);
        audioEngine.rebuildBridgeGraph (active);
    }

    // Returns up to recentBridgeCount recent bridge plugin files (newest first).
    juce::Array<juce::File> getRecentBridgeFiles()
    {
        auto* prefs = appProperties.getUserSettings();
        if (prefs == nullptr) return {};
        const int maxDisplay = prefs->getIntValue ("recentBridgeCount", 5);
        auto parts = juce::StringArray::fromTokens (prefs->getValue ("recentBridgeFiles"), "|", "");
        juce::Array<juce::File> result;
        for (int i = 0; i < juce::jmin (maxDisplay, parts.size()); ++i)
            if (parts[i].isNotEmpty())
                result.add (juce::File (parts[i]));
        return result;
    }

    // Prepends file to the stored recent list (newest first), capped at 20 entries.
    void addToRecentBridgeFiles (const juce::File& file)
    {
        auto* prefs = appProperties.getUserSettings();
        if (prefs == nullptr) return;
        auto parts = juce::StringArray::fromTokens (prefs->getValue ("recentBridgeFiles"), "|", "");
        parts.removeString (file.getFullPathName());
        parts.insert (0, file.getFullPathName());
        while (parts.size() > 20) parts.remove (parts.size() - 1);
        prefs->setValue ("recentBridgeFiles", parts.joinIntoString ("|"));
    }

    // Common bridge launch logic used by both the file chooser and recent-file menu.
    void launchBridgeWithPath (const juce::File& pluginFile)
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

        auto* bridge = bridges.add (new BridgeInstance());
        juce::String pluginName = pluginFile.getFileNameWithoutExtension();

        bridge->onConnected = [this, pluginName] (BridgeInstance* b) {
            auto& setup = deviceManager.getAudioDeviceSetup();
            auto sr = static_cast<float> (setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0);
            auto bs = setup.bufferSize > 0 ? setup.bufferSize : 512;
            b->sendAudioConfig (sr, bs);
            if (auto* mc = mainComp())
                mc->pushSystemMessage ("Bridge connected: " + pluginName);
            juce::MessageManager::callAsync ([this] { rebuildBridgeGraph(); });
        };

        bridge->onDisconnected = [this, pluginName] (BridgeInstance* b) {
            juce::MessageManager::callAsync ([this, b, pluginName] {
                if (auto* mc = mainComp())
                    mc->pushSystemMessage ("Bridge disconnected: " + pluginName);
                // If this bridge was the solo MIDI target, fall back to All mode.
                if (midiTargetBridge.load() == b)
                {
                    midiTargetBridge.store (nullptr);
                    routeToAll.store (true);
                    if (auto* mc = mainComp())
                        mc->pushSystemMessage ("MIDI Route reset to: All Bridges");
                }
                // Rebuild the graph BEFORE removing the bridge (state is already Idle,
                // so this bridge is excluded from the active list automatically).
                rebuildBridgeGraph();
                bridges.removeObject (b);
            });
        };

        if (auto* mc = mainComp())
            mc->pushSystemMessage ("Launching bridge: " + pluginName);
        bridge->launch (pluginFile.getFullPathName(), bridgeExe);

        addToRecentBridgeFiles (pluginFile);
        if (auto* prefs = appProperties.getUserSettings())
            if (prefs->getBoolValue ("rememberLastFolder", true))
                prefs->setValue ("lastBridgeFolder",
                                 pluginFile.getParentDirectory().getFullPathName());
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

    // MIDI routing state.
    // Accessed from MIDI input thread and message thread — use atomics.
    std::atomic<bool>            routeToAll       { true };
    std::atomic<BridgeInstance*> midiTargetBridge { nullptr };

    MidiKeyboardState keyboardState;
    AudioDeviceManager deviceManager;
    KnownPluginList knownPlugins;
    AudioEngine audioEngine { keyboardState };
    ApplicationProperties appProperties;
    juce::OwnedArray<BridgeInstance> bridges;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PCKeyboardListener> pcKeyListener;
    std::unique_ptr<PluginScanThread> scanThread;
    std::shared_ptr<std::atomic<bool>> scanToken;
    std::unique_ptr<SettingsWindow> settingsWindow;
};

START_JUCE_APPLICATION (LvhProApplication)
