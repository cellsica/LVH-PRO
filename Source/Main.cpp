#include "MainComponent.h"
#include "SettingsWindow.h"
#include "AudioEngine.h"
#include "PluginScanThread.h"
#include "PluginSlot.h"
#include "BridgeInstance.h"
#include "MidiRoutingManager.h"

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
            for (auto* b : bridges)
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
            if (! bridges.isEmpty())
            {
                routeSub.addSeparator();
                int rid = 4001;
                for (auto* b : bridges)
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
                else if (result == 5001)
                {
                    auto chooser = std::make_shared<juce::FileChooser> (
                        "Save Project...",
                        currentProjectFile.existsAsFile()
                            ? currentProjectFile.getParentDirectory()
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
                                currentProjectFile = f.withFileExtension ("lvh");
                                saveProject (currentProjectFile);
                            }
                        });
                }
                else if (result == 5002)
                {
                    auto chooser = std::make_shared<juce::FileChooser> (
                        "Open Project...",
                        currentProjectFile.existsAsFile()
                            ? currentProjectFile.getParentDirectory()
                            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                        "*.lvh");
                    chooser->launchAsync (
                        juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
                        [this, chooser] (const juce::FileChooser& fc) {
                            auto f = fc.getResult();
                            if (f.existsAsFile())
                            {
                                currentProjectFile = f;
                                loadProject (f);
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
                                launchBridgeWithPath (f, BridgeInstance::Role::Effect);
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
                        launchBridgeWithPath (result);
                });
        };
    }

    // Collect all Connected bridges, split by role, and rebuild the audio graph.
    // Instruments are mixed in parallel; Effects are chained serially after them.
    // Also refreshes the Mixer Console if it is open.
    // Falls back to sine wave if no bridges are connected.
    void rebuildBridgeGraph()
    {
        juce::Array<BridgeInstance*> instruments, effects;
        for (auto* b : bridges)
        {
            if (b->getState() != BridgeInstance::State::Connected)
                continue;
            if (b->getRole() == BridgeInstance::Role::Effect)
                effects.add (b);
            else
                instruments.add (b);
        }
        audioEngine.rebuildBridgeGraph (instruments, effects);

        // Keep the Mixer Console in sync with the current bridge list
        if (mixerWindow != nullptr)
            mixerWindow->updateBridges (instruments, effects);
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
    // role defaults to Instrument; pass Role::Effect to launch as an effect bridge.
    void launchBridgeWithPath (const juce::File& pluginFile,
                               BridgeInstance::Role role = BridgeInstance::Role::Instrument)
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
        bridge->setRole (role);
        juce::String pluginName = pluginFile.getFileNameWithoutExtension()
                                  + (role == BridgeInstance::Role::Effect ? " [FX]" : "");

        juce::String pluginPathStr = pluginFile.getFullPathName();
        bridge->onConnected = [this, pluginName, pluginPathStr] (BridgeInstance* b) {
            auto& setup = deviceManager.getAudioDeviceSetup();
            auto sr = static_cast<float> (setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0);
            auto bs = setup.bufferSize > 0 ? setup.bufferSize : 512;

            // Send SetState BEFORE AudioConfig so the Bridge can apply it
            // before prepareToPlay (correct VST3 restore order: setState → prepareToPlay).
            auto sit = pendingPluginStates.find (pluginPathStr);
            if (sit != pendingPluginStates.end())
            {
                if (auto* mc = mainComp())
                    mc->pushSystemMessage ("Restoring plugin state for: " + pluginName
                        + " (" + juce::String ((int) sit->second.getSize()) + " bytes)");
                b->sendSetState (sit->second);
                pendingPluginStates.erase (sit);
            }

            // Restore mixer settings before rebuildBridgeGraph so MixerStrip reads correct values
            auto mit = pendingMixerSettings.find (pluginPathStr);
            if (mit != pendingMixerSettings.end())
            {
                b->mixerGain.store     (mit->second.gain,     std::memory_order_relaxed);
                b->mixerPan.store      (mit->second.pan,      std::memory_order_relaxed);
                b->mixerMuted.store    (mit->second.muted,    std::memory_order_relaxed);
                b->mixerBypassed.store (mit->second.bypassed, std::memory_order_relaxed);
                b->mixerCustomName  = mit->second.customName;
                b->mixerCustomColor = mit->second.customColor;
                pendingMixerSettings.erase (mit);
            }

            b->sendAudioConfig (sr, bs);
            if (auto* mc = mainComp())
                mc->pushSystemMessage ("Bridge connected: " + pluginName);
            juce::MessageManager::callAsync ([this] { rebuildBridgeGraph(); });

            // Restore window position from project load
            auto it = pendingWindowBounds.find (pluginPathStr);
            if (it != pendingWindowBounds.end())
            {
                auto bounds = it->second;
                pendingWindowBounds.erase (it);
                juce::Timer::callAfterDelay (800, [b, bounds] {
                    b->sendWindowPos (bounds.getX(), bounds.getY(),
                                      bounds.getWidth(), bounds.getHeight());
                });
            }

            // Restore MIDI routing target
            midiRouter.tryApplyPendingTarget (pluginPathStr, b);
        };

        bridge->onDisconnected = [this, pluginName] (BridgeInstance* b) {
            juce::MessageManager::callAsync ([this, b, pluginName] {
                if (auto* mc = mainComp())
                    mc->pushSystemMessage ("Bridge disconnected: " + pluginName);
                // If this bridge was the solo MIDI target, fall back to All mode.
                if (midiRouter.handleBridgeDisconnected (b))
                    if (auto* mc = mainComp())
                        mc->pushSystemMessage ("MIDI Route reset to: All Bridges");
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

    // Collect plugin states from all connected bridges, then write the project XML.
    // Uses a shared counter to know when all responses have arrived (or a 2s timeout fires).
    void saveProject (const juce::File& file)
    {
        // Count connected bridges that can provide state
        juce::Array<BridgeInstance*> connected;
        for (auto* b : bridges)
            if (b->getState() == BridgeInstance::State::Connected)
                connected.add (b);

        if (connected.isEmpty())
        {
            writeProjectXml (file, {});
            return;
        }

        // Collect state responses asynchronously
        struct SaveContext
        {
            std::vector<BridgeStateEntry> entries;
            juce::File                    targetFile;
            int                           remaining = 0;
            bool                          written   = false;
        };
        auto ctx = std::make_shared<SaveContext>();
        ctx->targetFile  = file;
        ctx->remaining   = connected.size();
        for (auto* b : connected)
            ctx->entries.push_back ({ b, {}, false });

        // Timeout: write whatever we have after 2 seconds
        juce::Timer::callAfterDelay (2000, [this, ctx] {
            if (! ctx->written)
            {
                ctx->written = true;
                writeProjectXml (ctx->targetFile, ctx->entries);
            }
        });

        // Wire onStateReceived for each connected bridge
        for (int i = 0; i < (int) ctx->entries.size(); ++i)
        {
            auto* b = ctx->entries[i].bridge;
            b->onStateReceived = [this, ctx, i] (BridgeInstance*, const juce::MemoryBlock& state) {
                if (ctx->written) return;
                ctx->entries[i].state    = state;
                ctx->entries[i].received = true;
                if (--ctx->remaining <= 0)
                {
                    ctx->written = true;
                    writeProjectXml (ctx->targetFile, ctx->entries);
                }
            };
            b->sendRequestState();
        }

        if (auto* mc = mainComp())
            mc->pushSystemMessage ("Saving project (collecting plugin states)...");
    }

    struct BridgeStateEntry { BridgeInstance* bridge; juce::MemoryBlock state; bool received = false; };

    void writeProjectXml (const juce::File& file,
                          const std::vector<BridgeStateEntry>& stateEntries)
    {
        auto xml = std::make_unique<XmlElement> ("LVH-Project");
        xml->setAttribute ("version", 1);

        auto* bridgesEl = xml->createNewChildElement ("Bridges");
        for (auto* b : bridges)
        {
            if (b->getState() == BridgeInstance::State::Connected)
            {
                auto* el = bridgesEl->createNewChildElement ("Bridge");
                el->setAttribute ("plugin", b->getPluginPath());
                el->setAttribute ("role", b->getRole() == BridgeInstance::Role::Effect
                                          ? "effect" : "instrument");
                auto bounds = b->getWindowBounds();
                el->setAttribute ("x", bounds.getX());
                el->setAttribute ("y", bounds.getY());
                el->setAttribute ("w", bounds.getWidth());
                el->setAttribute ("h", bounds.getHeight());

                // Mixer state
                el->setAttribute ("gain",     (double) b->mixerGain.load());
                el->setAttribute ("pan",      (double) b->mixerPan.load());
                el->setAttribute ("muted",    b->mixerMuted.load()    ? 1 : 0);
                el->setAttribute ("bypassed", b->mixerBypassed.load() ? 1 : 0);
                if (b->mixerCustomName.isNotEmpty())
                    el->setAttribute ("customName", b->mixerCustomName);
                if (b->mixerCustomColor.getAlpha() > 0)
                    el->setAttribute ("customColor", b->mixerCustomColor.toDisplayString (true));

                // Plugin state (base64 encoded)
                for (const auto& entry : stateEntries)
                    if (entry.bridge == b && entry.received && entry.state.getSize() > 0)
                    {
                        el->setAttribute ("state", juce::Base64::toBase64 (
                            entry.state.getData(), entry.state.getSize()));
                        break;
                    }
            }
        }

        auto* routeEl = xml->createNewChildElement ("MidiRouting");
        routeEl->setAttribute ("routeToAll", midiRouter.isRouteToAll() ? 1 : 0);
        if (! midiRouter.isRouteToAll())
            if (auto* t = midiRouter.getTarget())
                routeEl->setAttribute ("targetPlugin", t->getPluginPath());

        auto* settingsEl = xml->createNewChildElement ("Settings");
        settingsEl->setAttribute ("octaveOffset", midiRouter.getOctaveOffset());
        double savedVolume = mainComp() ? mainComp()->getVolumeSlider().getValue() : masterVolume;
        settingsEl->setAttribute ("masterVolume", savedVolume);
        if (auto* prefs = appProperties.getUserSettings())
        {
            settingsEl->setAttribute ("transpose",     prefs->getIntValue ("transpose",     0));
            settingsEl->setAttribute ("channelFilter", prefs->getIntValue ("channelFilter", 0));
        }

        // Core (main) window position
        if (mainWindow != nullptr)
        {
            auto b = mainWindow->getBounds();
            settingsEl->setAttribute ("coreWindowX", b.getX());
            settingsEl->setAttribute ("coreWindowY", b.getY());
            settingsEl->setAttribute ("coreWindowW", b.getWidth());
            settingsEl->setAttribute ("coreWindowH", b.getHeight());
        }

        // Save enabled MIDI input device (identifier + name for fallback matching)
        for (auto& d : juce::MidiInput::getAvailableDevices())
        {
            if (deviceManager.isMidiInputDeviceEnabled (d.identifier))
            {
                settingsEl->setAttribute ("midiInputIdentifier", d.identifier);
                settingsEl->setAttribute ("midiInputName",       d.name);
                break;
            }
        }

        // Mixer Console position + visibility
        settingsEl->setAttribute ("mixerVisible", (mixerWindow != nullptr && mixerWindow->isVisible()) ? 1 : 0);
        if (mixerWindow != nullptr)
        {
            auto b = mixerWindow->getBounds();
            settingsEl->setAttribute ("mixerWindowX", b.getX());
            settingsEl->setAttribute ("mixerWindowY", b.getY());
            settingsEl->setAttribute ("mixerWindowW", b.getWidth());
            settingsEl->setAttribute ("mixerWindowH", b.getHeight());
        }

        xml->writeTo (file);
        if (auto* mc = mainComp())
        {
            int savedStateCount = 0;
            for (const auto& entry : stateEntries)
                if (entry.received && entry.state.getSize() > 0) ++savedStateCount;

                mc->pushSystemMessage ("Project saved: " + file.getFileNameWithoutExtension()
                + " (" + juce::String (savedStateCount) + "/"
                + juce::String (stateEntries.size()) + " plugin states captured)");
        }

        // Clear state callbacks
        for (auto& entry : stateEntries)
            if (entry.bridge != nullptr)
                entry.bridge->onStateReceived = nullptr;
    }

    void loadProject (const juce::File& file)
    {
        if (! file.existsAsFile()) return;

        auto xml = XmlDocument::parse (file);
        if (xml == nullptr || xml->getTagName() != "LVH-Project")
        {
            if (auto* mc = mainComp())
                mc->pushSystemMessage ("Failed to load project: " + file.getFileName());
            return;
        }

        // Shut down existing bridges
        audioEngine.buildGraphWithSineWave();
        bridges.clear();
        pendingWindowBounds.clear();
        pendingPluginStates.clear();
        pendingMixerSettings.clear();
        midiRouter.resetForProjectLoad();

        // Restore settings
        if (auto* settingsEl = xml->getChildByName ("Settings"))
        {
            int targetOctave = settingsEl->getIntAttribute ("octaveOffset", 0);
            midiRouter.applyOctaveShift (targetOctave - midiRouter.getOctaveOffset());

            int transpose = settingsEl->getIntAttribute ("transpose", 0);
            int channel   = settingsEl->getIntAttribute ("channelFilter", 0);
            audioEngine.setTranspose (transpose);
            audioEngine.setChannelFilter (channel);
            if (auto* prefs = appProperties.getUserSettings())
            {
                prefs->setValue ("transpose",     transpose);
                prefs->setValue ("channelFilter", channel);
            }

            masterVolume = settingsEl->getDoubleAttribute ("masterVolume", 1.0);
            if (auto* mc = mainComp())
            {
                mc->getVolumeSlider().setValue (masterVolume, dontSendNotification);
                mc->setVolumeDisplay (masterVolume);
                audioEngine.setOutputGain ((float) masterVolume);
                mc->pushSystemMessage ("  masterVol loaded: " + juce::String (masterVolume, 3));
            }
            if (mixerWindow != nullptr) mixerWindow->setMasterGain ((float) masterVolume);

            // Restore Core window position
            int coreW = settingsEl->getIntAttribute ("coreWindowW", 0);
            int coreH = settingsEl->getIntAttribute ("coreWindowH", 0);
            if (mainWindow != nullptr && coreW > 200 && coreH > 100)
            {
                int coreX = settingsEl->getIntAttribute ("coreWindowX", 0);
                int coreY = settingsEl->getIntAttribute ("coreWindowY", 0);
                mainWindow->setBounds (coreX, coreY, coreW, coreH);
            }

            // Restore MIDI input device (match by identifier first, then by name)
            {
                juce::String savedId   = settingsEl->getStringAttribute ("midiInputIdentifier");
                juce::String savedName = settingsEl->getStringAttribute ("midiInputName");
                if (savedId.isNotEmpty() || savedName.isNotEmpty())
                {
                    auto midiDevices = juce::MidiInput::getAvailableDevices();
                    juce::String targetId;

                    // Prefer identifier match
                    for (auto& d : midiDevices)
                        if (d.identifier == savedId) { targetId = d.identifier; break; }

                    // Fallback: name match (handles device re-enumeration across OS restarts)
                    if (targetId.isEmpty() && savedName.isNotEmpty())
                        for (auto& d : midiDevices)
                            if (d.name == savedName) { targetId = d.identifier; break; }

                    if (targetId.isNotEmpty())
                    {
                        for (auto& d : midiDevices)
                            deviceManager.setMidiInputDeviceEnabled (d.identifier, false);
                        deviceManager.setMidiInputDeviceEnabled (targetId, true);
                        if (auto* mc = mainComp())
                            mc->pushSystemMessage ("MIDI IN restored: " + savedName);
                    }
                }
            }

            // Restore Mixer Console visibility + position
            bool mixerWasVisible = settingsEl->getIntAttribute ("mixerVisible", 0) != 0;
            int mixerW = settingsEl->getIntAttribute ("mixerWindowW", 0);
            int mixerH = settingsEl->getIntAttribute ("mixerWindowH", 0);
            if (mixerWasVisible)
            {
                toggleMixerWindow (true);
                if (mixerWindow != nullptr && mixerW > 100 && mixerH > 50)
                {
                    int mixerX = settingsEl->getIntAttribute ("mixerWindowX", 0);
                    int mixerY = settingsEl->getIntAttribute ("mixerWindowY", 0);
                    mixerWindow->setBounds (mixerX, mixerY, mixerW, mixerH);
                }
                if (auto* mc = mainComp())
                    mc->setMixerWindowVisible (true);
            }
            else
            {
                if (mixerWindow != nullptr)
                    mixerWindow->setVisible (false);
                if (auto* mc = mainComp())
                    mc->setMixerWindowVisible (false);
            }
        }

        // Restore MIDI routing state
        if (auto* routeEl = xml->getChildByName ("MidiRouting"))
        {
            bool allMode = routeEl->getIntAttribute ("routeToAll", 1) != 0;
            if (allMode)
                midiRouter.setRouteToAll();
            else
                midiRouter.setPendingTarget (routeEl->getStringAttribute ("targetPlugin"));
        }

        // Launch bridges
        if (auto* bridgesEl = xml->getChildByName ("Bridges"))
        {
            for (auto* el : bridgesEl->getChildWithTagNameIterator ("Bridge"))
            {
                juce::String pluginPath = el->getStringAttribute ("plugin");
                juce::String roleStr    = el->getStringAttribute ("role", "instrument");
                auto role = (roleStr == "effect") ? BridgeInstance::Role::Effect
                                                  : BridgeInstance::Role::Instrument;
                int x = el->getIntAttribute ("x", 0);
                int y = el->getIntAttribute ("y", 0);
                int w = el->getIntAttribute ("w", 0);
                int h = el->getIntAttribute ("h", 0);

                if (pluginPath.isNotEmpty())
                {
                    if (w > 0 && h > 0)
                        pendingWindowBounds[pluginPath] = { x, y, w, h };

                    // Decode and store plugin state for restoration after connect
                    juce::String stateB64 = el->getStringAttribute ("state");
                    if (stateB64.isNotEmpty())
                    {
                        juce::MemoryBlock stateBytes;
                        juce::MemoryOutputStream mos (stateBytes, false);
                        juce::Base64::convertFromBase64 (mos, stateB64);
                        if (stateBytes.getSize() > 0)
                            pendingPluginStates[pluginPath] = stateBytes;
                    }

                    // Store mixer settings for restoration after connect
                    MixerSettings ms;
                    ms.gain     = (float) el->getDoubleAttribute ("gain",  1.0);
                    ms.pan      = (float) el->getDoubleAttribute ("pan",   0.0);
                    ms.muted    = el->getIntAttribute ("muted",    0) != 0;
                    ms.bypassed = el->getIntAttribute ("bypassed", 0) != 0;
                    ms.customName = el->getStringAttribute ("customName");
                    juce::String colorStr = el->getStringAttribute ("customColor");
                    if (colorStr.isNotEmpty())
                        ms.customColor = juce::Colour::fromString (colorStr);
                    pendingMixerSettings[pluginPath] = ms;

                    juce::File pluginFile (pluginPath);
                    if (pluginFile.exists())
                        launchBridgeWithPath (pluginFile, role);
                    else if (auto* mc = mainComp())
                        mc->pushSystemMessage ("Plugin not found: " + pluginFile.getFileName());
                }
            }
        }

        if (auto* mc = mainComp())
            mc->pushSystemMessage ("Project loaded: " + file.getFileNameWithoutExtension());
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

    // Project persistence
    struct MixerSettings
    {
        float        gain     = 1.f;
        float        pan      = 0.f;
        bool         muted    = false;
        bool         bypassed = false;
        juce::String customName;
        juce::Colour customColor { juce::Colours::transparentBlack };
    };
    juce::File                                      currentProjectFile;
    std::map<juce::String, juce::Rectangle<int>>    pendingWindowBounds;
    std::map<juce::String, juce::MemoryBlock>       pendingPluginStates;
    std::map<juce::String, MixerSettings>           pendingMixerSettings;
    MidiKeyboardState keyboardState;
    AudioDeviceManager deviceManager;
    KnownPluginList knownPlugins;
    AudioEngine audioEngine { keyboardState };
    ApplicationProperties appProperties;
    juce::OwnedArray<BridgeInstance> bridges;
    MidiRoutingManager               midiRouter { bridges };  // must be declared after bridges
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PCKeyboardListener> pcKeyListener;
    std::unique_ptr<PluginScanThread> scanThread;
    std::shared_ptr<std::atomic<bool>> scanToken;
    std::unique_ptr<SettingsWindow> settingsWindow;
};

START_JUCE_APPLICATION (LvhProApplication)
