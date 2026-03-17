#include "MainComponent.h"
#include "SettingsWindow.h"
#include "AudioEngine.h"
#include "PluginScanThread.h"
#include "PluginSlot.h"
#include "IPCManager.h"

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
        // Forward to Bridge via IPC (non-blocking; no-op if not connected)
        ipcManager.sendMidi (message);
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
            ipcManager.sendMidi (msg);
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
            ipcManager.sendMidi (msg);
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

        if (auto* mc = mainComp()) mc->setPluginEditor (nullptr);

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

        // LVH logo right-click: Settings + MIDI Input
        mc->onLogoRightClick = [this] {
            PopupMenu m;
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
            m.addSeparator();
            m.addItem (1, "Settings...");
            m.addSeparator();
            m.addItem (2, "[Pro] Launch Test Bridge");
            m.showMenuAsync (PopupMenu::Options(), [this, midiInputs] (int result) {
                if (result == 1)
                {
                    openSettings();
                }
                else if (result == 2)
                {
                    if (auto* mc = mainComp()) mc->onLaunchBridgeClicked();
                }
                else if (result >= 1000)
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
            });
        };

        mc->onPluginMenuRequest = [this, mc] {
            PopupMenu m;
            int id = 1;
            for (auto& type : knownPlugins.getTypes()) m.addItem (id++, type.name);
            m.addSeparator();
            const int refreshId = id;
            m.addItem (refreshId, "Refresh Plugin List...");
            m.showMenuAsync (PopupMenu::Options(), [this, mc, refreshId] (int result) {
                if (result == refreshId)      startPluginScan();
                else if (result > 0)          mc->setPluginName (knownPlugins.getTypes()[result - 1].name, result);
            });
        };

        mc->onPresetMenuRequest = [this] {
            auto* slot = audioEngine.getSlot();
            if (slot == nullptr || ! slot->isLoaded()) return;
            String currentPresetName = slot->getCurrentPresetName();
            File   currentPresetFile = slot->getCurrentPresetFile();
            String pluginName        = slot->getProcessor()->getName();

            Array<File> presetFiles;
            auto folder = PluginSlot::getPresetsFolder (pluginName);
            if (folder.isDirectory())
                folder.findChildFiles (presetFiles, File::findFiles, false, "*.xml");

            struct Sorter { static int compareElements (const File& a, const File& b)
                { return a.getFileName().compareIgnoreCase (b.getFileName()); } };
            Sorter sorter; presetFiles.sort (sorter);

            PopupMenu m;
            m.addItem (1, "Save New Preset...");
            if (currentPresetName.isNotEmpty())
                m.addItem (2, "Overwrite \"" + currentPresetName + "\"");
            m.addSeparator();
            const int baseId = 100;
            for (int i = 0; i < presetFiles.size(); ++i)
                m.addItem (baseId + i, presetFiles[i].getFileNameWithoutExtension(),
                           true, presetFiles[i] == currentPresetFile);

            m.showMenuAsync (PopupMenu::Options(), [this, presetFiles, baseId] (int result) {
                if (result == 1)
                {
                    auto* dlg = new AlertWindow ("Save Preset", "Enter a name for this preset:",
                                                 MessageBoxIconType::NoIcon);
                    dlg->addTextEditor ("name", "");
                    dlg->addButton ("Save",   1, KeyPress (KeyPress::returnKey));
                    dlg->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
                    dlg->enterModalState (true,
                        ModalCallbackFunction::create ([this, dlg] (int res) {
                            if (res == 1) {
                                String name = dlg->getTextEditorContents ("name").trim();
                                if (name.isNotEmpty())
                                    if (auto* s = audioEngine.getSlot())
                                        s->savePreset (name);
                            }
                        }), true);
                }
                else if (result == 2)
                {
                    if (auto* s = audioEngine.getSlot()) s->savePreset (s->getCurrentPresetName());
                }
                else if (result >= baseId)
                {
                    int idx = result - baseId;
                    if (idx < presetFiles.size())
                        if (auto* s = audioEngine.getSlot()) s->loadPreset (presetFiles[idx]);
                }
            });
        };

        mc->onLoadClicked = [this] {
            auto* mc = mainComp();
            if (mc == nullptr) return;
            int index = mc->getSelectedPluginID() - 1;
            if (index < 0 || index >= knownPlugins.getNumTypes()) return;
            mc->setPluginEditor (nullptr);
            mainWindow->setName (getApplicationName() + "  \xe2\x80\x94  Loading...");
            auto& setup = deviceManager.getAudioDeviceSetup();
            audioEngine.loadPlugin (*knownPlugins.getType (index), setup.sampleRate, setup.bufferSize,
                [this] (bool success, const String& nameOrError) {
                    if (auto* mc = mainComp())
                    {
                        if (! success)
                        {
                            mainWindow->setName (getApplicationName() + "  \xe2\x80\x94  Error: " + nameOrError);
                            mc->setPresetButtonEnabled (false);
                            mc->setPluginLoaded (false);
                        }
                        else
                        {
                            mainWindow->setName (getApplicationName() + "  [" + nameOrError + "]");
                            mc->setPresetButtonEnabled (true);
                            mc->setPluginLoaded (true);
                            if (auto* slot = audioEngine.getSlot())
                                if (auto* editor = slot->getOrCreateEditor())
                                    mc->setPluginEditor (editor);
                        }
                    }
                });
        };

        mc->onUnloadClicked = [this] {
            if (auto* mc = mainComp())
            {
                mc->setPluginEditor (nullptr);
                mc->setPresetButtonEnabled (false);
                mc->setPluginLoaded (false);
            }
            audioEngine.unloadPlugin();
            mainWindow->setName (getApplicationName());
        };

        mc->onPanicClicked = [this] { audioEngine.allNotesOff(); };

        mc->onLaunchBridgeClicked = [this] {
            // プラグインファイルを選択させる
            auto chooser = std::make_shared<juce::FileChooser> ("Select a VST3 plugin to bridge...",
                                                          juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
                                                          "*.vst3");

            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, chooser] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    auto bridgeExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                         .getParentDirectory()
                                         .getChildFile ("LVH-Bridge.exe");

                    if (bridgeExe.existsAsFile())
                    {
                        // Generate a unique pipe name and start listening before launching Bridge
                        juce::String pipeName = "LVH-Bridge-" + juce::String (juce::Time::currentTimeMillis());
                        ipcManager.startPipe (pipeName);

                        juce::String args = "--plugin \"" + result.getFullPathName() + "\""
                                          + " --ipc-pipe " + pipeName;
                        bridgeExe.startAsProcess (args);
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                            "Bridge Error",
                            "LVH-Bridge.exe not found.");
                    }
                }
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

    MidiKeyboardState keyboardState;
    AudioDeviceManager deviceManager;
    KnownPluginList knownPlugins;
    AudioEngine audioEngine { keyboardState };
    ApplicationProperties appProperties;
    CoreIpcManager ipcManager;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PCKeyboardListener> pcKeyListener;
    std::unique_ptr<PluginScanThread> scanThread;
    std::shared_ptr<std::atomic<bool>> scanToken;
    std::unique_ptr<SettingsWindow> settingsWindow;
};

START_JUCE_APPLICATION (LvhProApplication)
