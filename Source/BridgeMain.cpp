#include <JuceHeader.h>
#include <memory>
#include "IPCManager.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

// =====================================================================
// 035-A: Process-level crash handler
// Captures unhandled exceptions (access violations, etc.) to bridge_crash.txt
// before the process terminates.
// =====================================================================
#if JUCE_WINDOWS
static LONG WINAPI bridgeUnhandledExceptionFilter (EXCEPTION_POINTERS* ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    void* addr = ep->ExceptionRecord->ExceptionAddress;

    juce::String msg;
    msg << "[CRASH] Unhandled exception at " << juce::Time::getCurrentTime().toString (true, true, true) << "\n"
        << "Exception code: 0x" << juce::String::toHexString (static_cast<int64> (code)) << "\n"
        << "Address:        0x" << juce::String::toHexString (reinterpret_cast<int64> (addr)) << "\n";

    // Write to crash log directly (Logger may be in bad state on hard crash)
    auto crashLog = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                        .getParentDirectory().getChildFile ("bridge_crash.txt");
    crashLog.appendText (msg);

    // Also attempt Logger (may succeed if crash is in audio thread, not message thread)
    juce::Logger::writeToLog (msg);

    return EXCEPTION_CONTINUE_SEARCH; // let Windows generate a crash report
}

// SEH helper: wraps processBlock in __try/__except so C++ destructors still work
// in BridgeAudioThread::run(). Must be a separate non-class function.
static DWORD callProcessBlockSEH (juce::AudioPluginInstance* plugin,
                                   juce::AudioBuffer<float>&  buffer,
                                   juce::MidiBuffer&          midiBuffer) noexcept
{
    __try
    {
        plugin->processBlock (buffer, midiBuffer);
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode();
    }
}
#endif

// =====================================================================
// BridgeAudioThread
//
// High-priority thread that handles real-time audio processing for Bridge.
// Waits on SyncEvents::waitForRequest(), processes audio via pluginInstance,
// writes output back to shared memory, then signals SyncEvents::signalDone().
// =====================================================================
class BridgeAudioThread : public juce::Thread
{
public:
    BridgeAudioThread (juce::AudioPluginInstance*    plugin,
                       SharedMemoryBuffer&           shm,
                       SyncEvents&                   events,
                       juce::MidiMessageCollector&   midiCollector)
        : Thread ("BridgeAudio"), plugin (plugin), shm (shm),
          events (events), midiCollector (midiCollector)
    {}

    void run() override
    {
        // 035-A: Set high thread priority for stable audio processing
        setPriority (juce::Thread::Priority::highest);

        // --- Pre-allocate audio buffer (avoids per-block heap allocation) -----------
        // We wait for the first layout to arrive so we know the real buffer size and
        // the plugin's actual output channel count (after forceStereoLayout).
        // Both are stable for the lifetime of this thread.
        int shmChannels = 2;
        int bufChannels = 2;
        int numSamplesFixed = 512; // fallback; overwritten from SHM on first block
        juce::AudioBuffer<float> audioBuffer;  // allocated once, reused every block

        juce::Logger::writeToLog ("[BridgeAudio] Thread started (plugin="
                                  + (plugin != nullptr ? plugin->getName() : "null") + ").");

        while (! threadShouldExit())
        {
            // Wait for Core to request processing (100ms timeout keeps the loop responsive)
            if (! events.waitForRequest (100))
                continue;

            auto* layout = shm.getLayout();
            if (layout == nullptr)
            {
                events.signalDone();
                continue;
            }
            if (plugin == nullptr)
            {
                // プラグイン未ロード時: audioIn をそのまま audioOut にパススルー
                // (nullのままsignalDoneするとaudioOutがゼロになりFXチェーンが無音になる)
                const int ns = juce::jmin ((int) layout->bufferSize, 4096);
                for (int ch = 0; ch < 2; ++ch)
                    std::memcpy (layout->audioOut[ch], layout->audioIn[ch],
                                 (size_t) ns * sizeof (float));
                events.signalDone();
                continue;
            }

            const int numSamples = layout->bufferSize;

            // Allocate the reusable buffer on the very first block (real layout known).
            if (firstBlock)
            {
                firstBlock    = false;
                shmChannels   = juce::jmin (2, (int) layout->numChannels);
                // Plugin may have more output channels than SHM supports (multi-out drum
                // machines etc.).  Provide enough channels so processBlock never overruns,
                // but only copy the first shmChannels back to shared memory.
                bufChannels   = juce::jmax (plugin->getTotalNumOutputChannels(), shmChannels);
                numSamplesFixed = numSamples;
                audioBuffer.setSize (bufChannels, numSamples, false, true, false);

                juce::MidiBuffer tmpMidi;
                midiCollector.removeNextBlockOfMessages (tmpMidi, numSamples);
                juce::Logger::writeToLog ("[BridgeAudio] First processBlock: numSamples="
                                          + juce::String (numSamples)
                                          + " pluginOutCh=" + juce::String (bufChannels)
                                          + " midiEvents=" + juce::String (tmpMidi.getNumEvents()));
                // Fall through: process this first block normally with the pre-allocated buffer.
                // (tmpMidi is discarded — the note hasn't arrived yet at frame 0.)
            }

            // Fill buffer: clear all channels then copy SHM audio input.
            audioBuffer.clear();
            for (int ch = 0; ch < shmChannels; ++ch)
                std::memcpy (audioBuffer.getWritePointer (ch),
                             layout->audioIn[ch],
                             (size_t) numSamples * sizeof (float));

            // Collect MIDI events that arrived via IPC since last block
            juce::MidiBuffer midiBuffer;
            midiCollector.removeNextBlockOfMessages (midiBuffer, numSamples);

            if (firstMidiBlock && midiBuffer.getNumEvents() > 0)
            {
                firstMidiBlock = false;
                for (const auto meta : midiBuffer)
                    juce::Logger::writeToLog ("[BridgeAudio] First MIDI in buffer: "
                                              + meta.getMessage().getDescription());
            }

            // 035-A: Guard against plugin crashes in processBlock.
            // SEH catches access violations / structured exceptions (Windows).
            // C++ catch handles plugins that throw std::exception.
            // On any fault: log, signal done to unblock Core, then exit the thread.
#if JUCE_WINDOWS
            DWORD sehCode = callProcessBlockSEH (plugin, audioBuffer, midiBuffer);
            if (sehCode != 0)
            {
                juce::Logger::writeToLog ("[BridgeAudio] CRASH in processBlock! SEH code: 0x"
                                          + juce::String::toHexString (static_cast<int64> (sehCode))
                                          + " plugin=" + plugin->getName());
                events.signalDone(); // unblock Core so it doesn't hang
                break;
            }
#else
            try
            {
                plugin->processBlock (audioBuffer, midiBuffer);
            }
            catch (const std::exception& e)
            {
                juce::Logger::writeToLog ("[BridgeAudio] Exception in processBlock: "
                                          + juce::String (e.what()));
                events.signalDone();
                break;
            }
            catch (...)
            {
                juce::Logger::writeToLog ("[BridgeAudio] Unknown exception in processBlock!");
                events.signalDone();
                break;
            }
#endif

            // Write first 2 channels back to shared memory.
            for (int ch = 0; ch < shmChannels; ++ch)
                std::memcpy (layout->audioOut[ch],
                             audioBuffer.getReadPointer (ch),
                             (size_t) numSamples * sizeof (float));

            // Signal Core that output is ready
            events.signalDone();
        }

        juce::Logger::writeToLog ("[BridgeAudio] Thread stopped.");
    }

private:
    juce::AudioPluginInstance*  plugin;
    SharedMemoryBuffer&         shm;
    SyncEvents&                 events;
    juce::MidiMessageCollector& midiCollector;
    bool                        firstBlock     = true;
    bool                        firstMidiBlock = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeAudioThread)
};

// =====================================================================
// LVH-Bridge: プラグインホスト用の子プロセス
// =====================================================================
class LvhBridgeApplication : public juce::JUCEApplication,
                             private juce::Timer
{
public:
    LvhBridgeApplication() {}

    const juce::String getApplicationName() override    { return "LVH-Bridge"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override    { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // ログの設定 (EXEと同じ場所)
        auto logFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                            .getParentDirectory().getChildFile ("bridge_log.txt");
        fileLogger.reset (new juce::FileLogger (logFile, "--- LVH-Bridge Log Started ---"));
        juce::Logger::setCurrentLogger (fileLogger.get());

        // 035-A: Install process-level crash handler to log unhandled exceptions
#if JUCE_WINDOWS
        SetUnhandledExceptionFilter (bridgeUnhandledExceptionFilter);
        juce::Logger::writeToLog ("[Bridge] Crash handler installed.");
#endif

        juce::Logger::writeToLog ("Command Line: " + commandLine);
        juce::Logger::writeToLog ("Bridge Architecture: " + juce::String (sizeof(void*) * 8) + "-bit");

        juce::String pluginPath;
        juce::String ipcPipeName;
        juce::String shmName;
        juce::String syncName;
        auto args = juce::StringArray::fromTokens (commandLine, true);

        for (int i = 0; i < args.size(); ++i)
        {
            if      (args[i] == "--plugin"    && i + 1 < args.size()) pluginPath  = args[i + 1].unquoted();
            else if (args[i] == "--ipc-pipe"  && i + 1 < args.size()) ipcPipeName = args[i + 1];
            else if (args[i] == "--shm-name"  && i + 1 < args.size()) shmName     = args[i + 1];
            else if (args[i] == "--sync-name" && i + 1 < args.size()) syncName    = args[i + 1];
        }

        juce::Logger::writeToLog ("Detected Plugin Path: " + pluginPath);
        juce::Logger::writeToLog ("IPC Pipe Name:   " + (ipcPipeName.isNotEmpty() ? ipcPipeName : "(none)"));
        juce::Logger::writeToLog ("Shared Mem Name: " + (shmName.isNotEmpty()     ? shmName     : "(none)"));
        juce::Logger::writeToLog ("Sync Event Name: " + (syncName.isNotEmpty()    ? syncName    : "(none)"));

        // 共有メモリをオープン
        if (shmName.isNotEmpty())
        {
            sharedMem = std::make_unique<SharedMemoryBuffer>();
            if (sharedMem->open (shmName))
                juce::Logger::writeToLog ("[Bridge] Shared memory opened successfully.");
            else
                juce::Logger::writeToLog ("[Bridge] Warning: failed to open shared memory.");
        }

        // 同期イベントをオープン
        if (syncName.isNotEmpty())
        {
            syncEvents = std::make_unique<SyncEvents>();
            if (syncEvents->open (syncName))
                juce::Logger::writeToLog ("[Bridge] Sync events opened successfully.");
            else
                juce::Logger::writeToLog ("[Bridge] Warning: failed to open sync events.");
        }

        // IPC接続を先に開始する。
        // VST3ロード（mainWindow生成）は数秒かかるため、後から接続すると
        // Core側のpipeReceiveMessageTimeout(5000ms)が切れて切断扱いになる。
        // connectAsync はバックグラウンドスレッドで動くので mainWindow 生成と並行実行できる。
        // onConnected/onAudioConfigReceived は callAsync 経由でメッセージスレッドに届くため、
        // mainWindow 生成完了後に処理される（null チェック不要になるが念のため残す）。
        if (ipcPipeName.isNotEmpty())
        {
            ipcClient = std::make_unique<BridgeIpcClient>();
            ipcClient->onConnected = [this] {
                juce::Logger::writeToLog ("[Bridge] IPC channel ready.");
                startTimer (1500); // send heartbeat every 1.5 s to keep Core's read alive
                // 035-A: Do NOT start the audio thread here.
                // AudioConfig (with sampleRate/bufferSize) must arrive first so that
                // prepareToPlay() is called before the first processBlock().
                // → startAudioThreadIfReady() is called at the end of onAudioConfigReceived.
            };
            ipcClient->onDisconnected = [this] {
                juce::Logger::writeToLog ("[Bridge] IPC channel closed by Core. Shutting down.");
                stopTimer();
                stopAudioThread();
                // Core closed the pipe (e.g. Core exited). Quit this Bridge process.
                systemRequestedQuit();
            };
            ipcClient->onMidiReceived = [this] (const juce::MidiMessage& msg) {
                // Log the very first MIDI message received (once only) for diagnostics.
                if (firstMidiReceived.exchange (true) == false)
                    juce::Logger::writeToLog ("[Bridge MIDI] First MIDI received: "
                                              + msg.getDescription());
                // MidiMessageCollector requires timestamps in seconds
                // (Time::getMillisecondCounterHiRes() * 0.001 epoch).
                // IPC-decoded messages have timestamp 0.0, so stamp them now.
                juce::MidiMessage timedMsg (msg);
                timedMsg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
                midiCollector.addMessageToQueue (timedMsg);
            };
            ipcClient->onAudioConfigReceived = [this] (float sr, int32_t bs) {
                audioConfigReceived = true;
                // 035-A: Send a heartbeat BEFORE prepareToPlay so Core's IPC pipe
                // receives data even if the plugin's prepareToPlay is very slow.
                // (Heavy plugins can block the message thread for several seconds.)
                ipcClient->sendHeartbeat();
                if (mainWindow != nullptr)
                {
                    // Apply any pending state BEFORE prepareToPlay.
                    // (VST3 correct restore order: setState → prepareToPlay)
                    if (pendingPluginState.getSize() > 0)
                    {
                        mainWindow->setPluginState (pendingPluginState);
                        pendingPluginState.reset();
                    }
                    mainWindow->preparePlugin (sr, bs);
                }
                // Reset MIDI collector with the actual sample rate
                midiCollector.reset (static_cast<double> (sr));
                // 035-A: Start audio thread HERE, after prepareToPlay has been called.
                // Starting in onConnected caused processBlock to fire before prepareToPlay,
                // which crashes plugins that require initialization before processing.
                startAudioThreadIfReady();
            };
            ipcClient->onWindowPosReceived = [this] (int x, int y, int w, int h) {
                if (mainWindow != nullptr && w > 0 && h > 0)
                {
                    mainWindow->setBounds (x, y, w, h);
                    mainWindow->setVisible (true);   // re-show if hidden via X button
                    mainWindow->toFront (false);
                }
            };
            ipcClient->onWindowTitleReceived = [this] (const juce::String& title) {
                if (mainWindow != nullptr && title.isNotEmpty())
                    mainWindow->setName (title);
            };
            ipcClient->onRequestStateReceived = [this] {
                if (mainWindow == nullptr) return;
                juce::MemoryBlock state;
                mainWindow->getPluginState (state);
                ipcClient->sendStateData (state);
                juce::Logger::writeToLog ("[Bridge] State sent: " + juce::String ((int) state.getSize()) + " bytes");
            };
            ipcClient->onSetStateReceived = [this] (const juce::MemoryBlock& state) {
                if (audioConfigReceived)
                {
                    // AudioConfig already received → plugin is prepared → apply immediately
                    if (mainWindow != nullptr)
                        mainWindow->setPluginState (state);
                }
                else
                {
                    // Buffer: apply before prepareToPlay in onAudioConfigReceived
                    pendingPluginState = state;
                }
            };
            ipcClient->connectAsync (ipcPipeName, 10000); // 10s: enough for Debug VST3 load
        }

        // VST3ロード（数秒かかる）。IPC接続はバックグラウンドで並行して進行する。
        mainWindow.reset (new MainWindow (getApplicationName(), pluginPath));
    }

    void timerCallback() override
    {
        if (ipcClient != nullptr)
        {
            ipcClient->sendHeartbeat();
            // Also report window position so Core can store it for project save
            if (mainWindow != nullptr)
            {
                auto b = mainWindow->getBounds();
                ipcClient->sendWindowPos (b.getX(), b.getY(), b.getWidth(), b.getHeight());
            }
        }
    }

    void shutdown() override
    {
        stopTimer();
        stopAudioThread();
        if (ipcClient != nullptr)
            ipcClient->disconnect();
        ipcClient.reset();
        syncEvents.reset();
        sharedMem.reset();
        mainWindow.reset();
        juce::Logger::setCurrentLogger (nullptr);
        fileLogger.reset();
    }

    void systemRequestedQuit() override { quit(); }

    // =================================================================
    // メインウィンドウクラス
    // =================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name, juce::String pluginPath)
            : DocumentWindow (name, 
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, true);

            if (pluginPath.isNotEmpty())
            {
                loadPlugin (pluginPath);
            }
            else
            {
                showStatusMessage ("No plugin specified.");
                juce::Logger::writeToLog ("Error: No plugin path provided in arguments.");
            }

            if (getContentComponent() == nullptr)
                centreWithSize (400, 300);
            
            // 画面中央に配置
            setCentreRelative (0.5f, 0.5f);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            // Hide the window instead of quitting — plugin lifetime is managed
            // by the Core via the Mixer Console Remove action.
            setVisible (false);
        }

        void preparePlugin (float sampleRate, int32_t bufferSize)
        {
            if (pluginInstance != nullptr)
            {
                // Log the plugin's default bus layout (diagnostic — helps identify
                // multi-output drum machines / samplers that need layout adjustment).
                {
                    auto bl = pluginInstance->getBusesLayout();
                    juce::String info = "[Bridge] Bus layout before prepare: ";
                    info << "in=" << bl.inputBuses.size() << " out=" << bl.outputBuses.size();
                    for (int i = 0; i < bl.outputBuses.size(); ++i)
                        info << " out[" << i << "]=" << bl.outputBuses[i].getDescription();
                    juce::Logger::writeToLog (info);
                }

                // Force stereo-only output before prepareToPlay.
                // Many drum machines / samplers have multiple output buses by default
                // (e.g. main mix + individual drum outs).  LVH-Bridge only provides a
                // 2-channel shared-memory buffer, so we disable all buses beyond the
                // first and ensure the first bus is stereo.  Plugins whose minimum
                // layout requires more channels will log a warning but are left as-is
                // (their audio output will likely be silent until multi-out is supported).
                forceStereoLayout();

                pluginInstance->prepareToPlay (sampleRate, static_cast<int> (bufferSize));
                juce::Logger::writeToLog ("[Bridge] prepareToPlay called: SR="
                                          + juce::String (sampleRate, 1)
                                          + " BS=" + juce::String (bufferSize));
            }
            else
            {
                juce::Logger::writeToLog ("[Bridge] AudioConfig received but plugin not loaded yet.");
            }
        }

        // Configure the plugin to use a single stereo output bus.
        // Called before prepareToPlay so the plugin initialises with the correct layout.
        void forceStereoLayout()
        {
            auto layout = pluginInstance->getBusesLayout();

            // Attempt: keep first output bus as stereo, disable the rest.
            if (layout.outputBuses.size() > 0)
                layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
            for (int i = 1; i < layout.outputBuses.size(); ++i)
                layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();

            if (pluginInstance->setBusesLayout (layout))
            {
                juce::Logger::writeToLog ("[Bridge] Bus layout forced to stereo output.");
            }
            else
            {
                // Plugin rejected the stereo-only layout (may require all buses active).
                // Try again with stereo on every bus so at least something plays.
                auto fullLayout = pluginInstance->getBusesLayout();
                for (int i = 0; i < fullLayout.outputBuses.size(); ++i)
                    fullLayout.outputBuses.getReference (i) = juce::AudioChannelSet::stereo();
                if (pluginInstance->setBusesLayout (fullLayout))
                    juce::Logger::writeToLog ("[Bridge] Bus layout: all outputs set to stereo.");
                else
                    juce::Logger::writeToLog ("[Bridge] Warning: could not adjust bus layout — plugin may be silent.");
            }
        }

        juce::AudioPluginInstance* getPluginInstance() const noexcept
        {
            return pluginInstance.get();
        }

        void getPluginState (juce::MemoryBlock& dest) const
        {
            dest.reset();
            if (pluginInstance != nullptr)
                pluginInstance->getStateInformation (dest);
        }

        void setPluginState (const juce::MemoryBlock& state)
        {
            if (pluginInstance != nullptr && state.getSize() > 0)
            {
                pluginInstance->setStateInformation (state.getData(), (int) state.getSize());
                juce::Logger::writeToLog ("[Bridge] Plugin state restored: "
                                          + juce::String ((int) state.getSize()) + " bytes");
            }
        }

    private:
        void loadPlugin (const juce::String& path)
        {
            juce::Logger::writeToLog ("Attempting to load: " + path);
            formatManager.addDefaultFormats();

            // VST3などの場合は、ファイルの中身をスキャンしてDescriptionを取得するのが確実
            juce::OwnedArray<juce::PluginDescription> types;
            for (int i = 0; i < formatManager.getNumFormats(); ++i)
                formatManager.getFormat(i)->findAllTypesForFile (types, path);

            juce::String error;
            
            if (types.size() > 0)
            {
                juce::Logger::writeToLog ("Found " + juce::String (types.size()) + " plugin types in file.");
                juce::Logger::writeToLog ("Loading first type: " + types[0]->name);
                
                pluginInstance = formatManager.createPluginInstance (*types[0], 48000.0, 480, error);
            }
            else
            {
                juce::Logger::writeToLog ("No plugin types found via scan. Falling back to manual description...");
                
                juce::PluginDescription desc;
                desc.fileOrIdentifier = path;
                if (path.endsWithIgnoreCase (".vst3")) desc.pluginFormatName = "VST3";
                else desc.pluginFormatName = "VST";
                
                pluginInstance = formatManager.createPluginInstance (desc, 48000.0, 480, error);
            }

            if (pluginInstance != nullptr)
            {
                juce::Logger::writeToLog ("Plugin instance created successfully: " + pluginInstance->getName());
                editor.reset (pluginInstance->createEditorIfNeeded());
                
                if (editor != nullptr)
                {
                    // エディタをウィンドウにセット
                    setContentNonOwned (editor.get(), true);
                    
                    // ウィンドウのタイトルをプラグイン名に変更
                    setName ("LVH-Bridge [" + pluginInstance->getName() + "]");
                    
                    // ウィンドウサイズをエディタに合わせる
                    auto bounds = editor->getLocalBounds();
                    if (! bounds.isEmpty())
                    {
                        setSize (bounds.getWidth(), bounds.getHeight());
                        juce::Logger::writeToLog ("Window resized to: " + juce::String (bounds.getWidth()) + "x" + juce::String (bounds.getHeight()));
                    }
                }
                else
                {
                    showStatusMessage ("Plugin loaded: " + pluginInstance->getName() + "\n(No Editor available)");
                    juce::Logger::writeToLog ("Warning: Plugin loaded but has no editor.");
                }
            }
            else
            {
                showStatusMessage ("Failed to load plugin:\n" + error + "\n\nPath:\n" + path);
                juce::Logger::writeToLog ("Error: Failed to create plugin instance. Details: " + error);
            }
        }

        void showStatusMessage (const juce::String& message)
        {
            auto* label = new juce::Label ("status", message);
            label->setJustificationType (juce::Justification::centred);
            label->setColour (juce::Label::textColourId, juce::Colours::white);
            setContentOwned (label, true);
        }

        juce::AudioPluginFormatManager formatManager;
        std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    void startAudioThreadIfReady()
    {
        if (sharedMem == nullptr || !sharedMem->isOpen()) return;
        if (syncEvents == nullptr || !syncEvents->isOpen()) return;
        if (mainWindow == nullptr) return;

        auto* plugin = mainWindow->getPluginInstance();
        if (plugin == nullptr) return;

        stopAudioThread();
        audioThread = std::make_unique<BridgeAudioThread> (plugin, *sharedMem, *syncEvents, midiCollector);
        audioThread->startThread();
        juce::Logger::writeToLog ("[Bridge] BridgeAudioThread started.");
    }

    void stopAudioThread()
    {
        if (audioThread != nullptr)
        {
            audioThread->signalThreadShouldExit();
            audioThread->stopThread (2000);
            audioThread.reset();
            juce::Logger::writeToLog ("[Bridge] BridgeAudioThread stopped.");
        }
    }

private:
    std::unique_ptr<juce::FileLogger>   fileLogger;
    std::unique_ptr<MainWindow>         mainWindow;
    std::unique_ptr<BridgeIpcClient>    ipcClient;
    std::unique_ptr<SharedMemoryBuffer> sharedMem;
    std::unique_ptr<SyncEvents>         syncEvents;
    std::unique_ptr<BridgeAudioThread>  audioThread;
    juce::MidiMessageCollector          midiCollector;
    juce::MemoryBlock                   pendingPluginState;   // state to apply before prepareToPlay
    bool                                audioConfigReceived = false;
    std::atomic<bool>                   firstMidiReceived   { false };
};

START_JUCE_APPLICATION (LvhBridgeApplication)
