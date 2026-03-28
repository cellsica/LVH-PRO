#include "MainComponent.h"
#if JUCE_WINDOWS
 #include <windows.h>
#endif
#include "SettingsWindow.h"
#include "AudioEngine.h"
#include "MetronomeManager.h"
#include "PluginScanThread.h"
#include "PluginSlot.h"
#include "BridgeInstance.h"
#include "MidiRoutingManager.h"
#include "ProjectSerializer.h"
#include "BridgeManager.h"
#include "UIManager.h"
#include "Core/StageManager.h"

// =====================================================================
// Main Application
// =====================================================================
class LvhProApplication : public JUCEApplication,
                                public MidiInputCallback,
                                public MidiKeyboardState::Listener
{
public:
    LvhProApplication() {}

    const String getApplicationName() override    { return "LVH"; }
    const String getApplicationVersion() override { return "0.7.0"; }
    bool moreThanOneInstanceAllowed() override    { return true; }

    // Hardware MIDI input
    void handleIncomingMidiMessage (MidiInput*, const MidiMessage& message) override
    {
        // Set flag BEFORE processNextMidiEvent so handleNoteOn/Off can detect
        // they were triggered by hardware and skip the duplicate midiRouter.sendMidi().
        fromHardwareMidi_.store (true, std::memory_order_relaxed);
        keyboardState.processNextMidiEvent (message);
        fromHardwareMidi_.store (false, std::memory_order_relaxed);
        // Route MIDI and update UI on the message thread.
        // IMPORTANT: midiRouter.sendMidi must run on the message thread so that
        // BridgeInstance::state (written by the IPC callback thread) is visible via
        // the happens-before relationship established by JUCE's message loop.
        // Calling it from the MIDI input thread caused a cache/visibility issue in
        // Release builds, making state appear Idle even after the bridge connected.
        auto msg = message;
        msg.setTimeStamp (Time::getMillisecondCounterHiRes() * 0.001);
        MessageManager::callAsync ([this, msg] {
            midiRouter.sendMidi (msg);
            uiManager_.handleMidiRemote (msg);
            if (auto* mc = mainComp()) mc->getMonitorPanel().pushMidiMessage (msg);
        });
    }

    // MidiKeyboardState::Listener — note events from PC keyboard / on-screen keyboard
    void handleNoteOn (MidiKeyboardState*, int channel, int note, float velocity) override
    {
        // Capture the hardware-MIDI flag NOW (on calling thread) before callAsync.
        // If true, handleIncomingMidiMessage already sent the note → don't duplicate.
        const bool fromHw = fromHardwareMidi_.load (std::memory_order_relaxed);
        MessageManager::callAsync ([this, channel, note, velocity, fromHw] {
            String name = MidiMessage::getMidiNoteName (note, true, true, 3);
            String text = "Note: " + String (note) + " (" + name + ")"
                        + " Vel: " + String (roundToInt (velocity * 127.f))
                        + " Ch: "  + String (channel);
            auto msg = MidiMessage::noteOn (channel, note, velocity);
            // MidiMessageCollector needs a valid timestamp (seconds since epoch).
            msg.setTimeStamp (Time::getMillisecondCounterHiRes() * 0.001);
            if (! fromHw)
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
        const bool fromHw = fromHardwareMidi_.load (std::memory_order_relaxed);
        MessageManager::callAsync ([this, channel, note, velocity, fromHw] {
            auto msg = MidiMessage::noteOff (channel, note, velocity);
            msg.setTimeStamp (Time::getMillisecondCounterHiRes() * 0.001);
            if (! fromHw)
                midiRouter.sendMidi (msg);
            if (auto* mc = mainComp())
                mc->getMonitorPanel().pushMidiMessage (msg);
        });
    }

    void initialise (const String&) override
    {
#if JUCE_WINDOWS
        // 035-A: Install crash handler so we get a log entry when Core crashes
        SetUnhandledExceptionFilter ([] (EXCEPTION_POINTERS* ep) -> LONG {
            DWORD code = ep->ExceptionRecord->ExceptionCode;
            void* addr = ep->ExceptionRecord->ExceptionAddress;
            juce::String msg;
            msg << "[CORE CRASH] Unhandled exception at "
                << juce::Time::getCurrentTime().toString (true, true, true) << "\n"
                << "Exception code: 0x" << juce::String::toHexString (static_cast<int64> (code)) << "\n"
                << "Address:        0x" << juce::String::toHexString (reinterpret_cast<int64> (addr)) << "\n";
            auto crashLog = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                .getParentDirectory().getChildFile ("core_crash.txt");
            crashLog.appendText (msg);
            juce::Logger::writeToLog (msg);
            return EXCEPTION_CONTINUE_SEARCH;
        });
#endif

        // 035-A: Enable Core-side logging so we can diagnose crashes on hobby PC
        {
            auto logFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                               .getParentDirectory().getChildFile ("core_log.txt");
            coreLogger_.reset (new juce::FileLogger (logFile, "--- LVH Core Log Started ---"));
            juce::Logger::setCurrentLogger (coreLogger_.get());
            juce::Logger::writeToLog ("Core started. Version: " + getApplicationVersion());
        }

        // Apply Japanese-capable fonts globally (Yu Gothic UI / MS Gothic)
        LookAndFeel::setDefaultLookAndFeel (&lvhLookAndFeel_);

        // Initialize settings persistence
        PropertiesFile::Options opts;
        opts.applicationName       = "LVH";
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

        uiManager_.setMainComponent (mainComp());
        wireUIManagerCallbacks();
        wireStageManagerCallbacks();
        wireUICallbacks();
        wireBridgeManagerCallbacks();
        wireSerializerCallbacks();
        uiManager_.restoreStageWindow();

        // Startup info → System Log
        if (auto* mc = mainComp())
        {
            mc->pushSystemMessage ("=== LVH-PRO v" + getApplicationVersion() + " ===");
            mc->pushSystemMessage ("OS:  " + juce::SystemStats::getOperatingSystemName());
            mc->pushSystemMessage ("CPU: " + juce::SystemStats::getCpuModel()
                                  + " (" + juce::String (juce::SystemStats::getNumCpus()) + " cores, "
                                  + juce::String (juce::SystemStats::getCpuSpeedInMegahertz()) + " MHz)");
            mc->pushSystemMessage ("RAM: " + juce::String (juce::SystemStats::getMemorySizeInMegabytes()) + " MB");
        }

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
        juce::Logger::writeToLog ("Core shutting down.");
        LookAndFeel::setDefaultLookAndFeel (nullptr);
        uiManager_.shutdown();
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

        juce::Logger::setCurrentLogger (nullptr);
        coreLogger_.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:

    File getCacheFile() const
    {
        return File::getSpecialLocation (File::userApplicationDataDirectory)
                   .getChildFile ("cellsica/LVH/KnownPlugins.xml");
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
                // Force cursor reset on all windows (e.g. MixerWindow may still show
                // the OS busy cursor after a scan triggered from its FX slot picker)
                for (int i = 0; i < Desktop::getInstance().getNumMouseSources(); ++i)
                    Desktop::getInstance().getMouseSource (i)->forceMouseCursorUpdate();
            });
        scanThread->startThread();
    }

    void wireUIManagerCallbacks()
    {
        uiManager_.onStartPluginScan = [this] { startPluginScan(); };
    }

    void wireStageManagerCallbacks()
    {
        stageManager_.onProjectLoadRequested = [this] (const juce::File& f,
                                                        bool isGlobal, bool globalLayerSwitch) {
            projectSerializer_.setCurrentProjectFile (f);
            projectSerializer_.loadProject (f, isGlobal, globalLayerSwitch);
        };

        stageManager_.onLoadError = [] (const StageManager::Item& item) {
            juce::NativeMessageBox::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "File Not Found",
                "Project file not found:\n\n" + item.path + "\n\n"
                "Please check that the file still exists at this location.",
                nullptr);
        };

        // onSetChanged is wired by UIManager when StageWindow is first opened
    }

    void wireBridgeManagerCallbacks()
    {
        bridgeManager_.onMessage = [this] (const juce::String& msg) {
            if (auto* mc = mainComp()) mc->pushSystemMessage (msg);
        };

        bridgeManager_.onGraphRebuilt = [this] (juce::Array<BridgeInstance*> instruments,
                                                  juce::Array<BridgeInstance*> effects) {
            uiManager_.updateMixerBridges (instruments, effects);
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

        projectSerializer_.onLaunchBridge = [this] (const juce::File& f, BridgeInstance::Role role,
                                                    const juce::String& fxParentPath, bool isGlobal) {
            // Closure injection: take all pending data here and pass by value to BridgeManager.
            // BridgeManager never needs to call back into ProjectSerializer.
            auto path = f.getFullPathName();
            bridgeManager_.launchBridgeWithPath (
                f, role,
                projectSerializer_.takePendingState  (path),
                projectSerializer_.takePendingMixer  (path),
                projectSerializer_.takePendingBounds (path),
                fxParentPath,
                isGlobal);
        };

        projectSerializer_.onProjectResetRequired = [this] (bool keepGlobal) {
            audioEngine.buildGraphWithSineWave();
            bridgeManager_.clearBridges (keepGlobal);
        };

        projectSerializer_.getMasterVolume = [this] () -> double {
            return uiManager_.getMasterVolume();
        };

        projectSerializer_.getCoreWindowBounds = [this] () -> juce::Rectangle<int> {
            return mainWindow != nullptr ? mainWindow->getBounds() : juce::Rectangle<int>{};
        };

        projectSerializer_.getMixerVisible = [this] () -> bool {
            return uiManager_.isMixerWindowVisible();
        };

        projectSerializer_.getMixerWindowBounds = [this] () -> juce::Rectangle<int> {
            return uiManager_.getMixerWindowBounds();
        };

        projectSerializer_.onMasterVolumeChanged = [this] (double vol) {
            uiManager_.setMasterVolume (vol);
        };

        projectSerializer_.onCoreWindowBoundsChanged = [this] (juce::Rectangle<int> b) {
            if (mainWindow != nullptr) mainWindow->setBounds (b);
        };

        projectSerializer_.onMixerWindowRestored = [this] (bool visible, juce::Rectangle<int> bounds) {
            uiManager_.restoreMixerWindow (visible, bounds);
        };

        projectSerializer_.onMetronomeSettingsRestored = [this] (double bpm, float vol, int bpb, int ct) {
            // AudioEngine is already updated by ProjectSerializer — just sync the open window (if any)
            uiManager_.syncMetronomeWindowFromEngine();
            // Also persist click type to prefs so Settings page stays consistent
            if (auto* prefs = appProperties.getUserSettings())
                prefs->setValue ("metronomeClickType", ct);
        };
    }

    void wireUICallbacks()
    {
        auto* mc = mainComp();
        if (mc == nullptr) return;

        // Level meter source
        mc->getLevelMeter().getPeak = [this] (int ch) { return audioEngine.exchangePeak (ch); };

        // Monitor panel: CPU usage source
        mc->getMonitorPanel().getCpuUsage = [this] { return deviceManager.getCpuUsage(); };

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

    std::unique_ptr<juce::FileLogger> coreLogger_;
    LvhLookAndFeel    lvhLookAndFeel_;
    MidiKeyboardState keyboardState;
    AudioDeviceManager deviceManager;
    KnownPluginList knownPlugins;
    AudioEngine audioEngine { keyboardState };
    MetronomeManager metronomeManager_ { audioEngine };
    ApplicationProperties appProperties;
    BridgeManager      bridgeManager_     { audioEngine, deviceManager, appProperties };
    MidiRoutingManager midiRouter         { bridgeManager_.getBridges() };
    ProjectSerializer  projectSerializer_ { bridgeManager_.getBridges(), audioEngine, midiRouter,
                                            deviceManager, appProperties };
    StageManager       stageManager_;
    std::unique_ptr<MainWindow> mainWindow;
    // UIManager declared after mainWindow → destroyed before mainWindow (reverse order)
    UIManager uiManager_ { audioEngine, bridgeManager_, projectSerializer_,
                            midiRouter, deviceManager, appProperties, knownPlugins,
                            stageManager_ };
    std::unique_ptr<PCKeyboardListener> pcKeyListener;
    std::unique_ptr<PluginScanThread> scanThread;
    std::shared_ptr<std::atomic<bool>> scanToken;
    // Set to true while handleIncomingMidiMessage() is processing hardware MIDI,
    // so handleNoteOn/Off skip the duplicate midiRouter.sendMidi() call.
    std::atomic<bool> fromHardwareMidi_ { false };
};

START_JUCE_APPLICATION (LvhProApplication)
