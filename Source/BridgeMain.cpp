#include <JuceHeader.h>
#include <memory>
#include "IPCManager.h"

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
        // TODO: set high thread priority when JUCE API is confirmed
        juce::Logger::writeToLog ("[BridgeAudio] Thread started.");

        while (! threadShouldExit())
        {
            // Wait for Core to request processing (100ms timeout keeps the loop responsive)
            if (! events.waitForRequest (100))
                continue;

            auto* layout = shm.getLayout();
            if (layout == nullptr || plugin == nullptr)
            {
                events.signalDone();
                continue;
            }

            const int numSamples  = layout->bufferSize;
            const int numChannels = juce::jmin (2, (int) layout->numChannels);

            // Build JUCE AudioBuffer from shared memory input
            juce::AudioBuffer<float> buffer (numChannels, numSamples);
            for (int ch = 0; ch < numChannels; ++ch)
                std::memcpy (buffer.getWritePointer (ch),
                             layout->audioIn[ch],
                             (size_t) numSamples * sizeof (float));

            // Collect MIDI events that arrived via IPC since last block
            juce::MidiBuffer midiBuffer;
            midiCollector.removeNextBlockOfMessages (midiBuffer, numSamples);

            // Debug: log first audio block received (once only)
            if (firstBlock)
            {
                firstBlock = false;
                juce::Logger::writeToLog ("[BridgeAudio] First processBlock: numSamples="
                                          + juce::String (numSamples)
                                          + " midiEvents=" + juce::String (midiBuffer.getNumEvents()));
            }

            plugin->processBlock (buffer, midiBuffer);

            // Write output back to shared memory
            for (int ch = 0; ch < numChannels; ++ch)
                std::memcpy (layout->audioOut[ch],
                             buffer.getReadPointer (ch),
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
    bool                        firstBlock = true;

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
                // AudioThread を開始 (共有メモリ・同期イベントが揃っている場合)
                startAudioThreadIfReady();
            };
            ipcClient->onDisconnected = [this] {
                juce::Logger::writeToLog ("[Bridge] IPC channel closed by Core. Shutting down.");
                stopTimer();
                stopAudioThread();
                // Core closed the pipe (e.g. Core exited). Quit this Bridge process.
                systemRequestedQuit();
            };
            ipcClient->onMidiReceived = [this] (const juce::MidiMessage& msg) {
                juce::Logger::writeToLog ("[Bridge MIDI] " + msg.getDescription());
                // Feed into the audio thread's MIDI queue
                midiCollector.addMessageToQueue (msg);
            };
            ipcClient->onAudioConfigReceived = [this] (float sr, int32_t bs) {
                if (mainWindow != nullptr)
                    mainWindow->preparePlugin (sr, bs);
                // Reset MIDI collector with the actual sample rate
                midiCollector.reset (static_cast<double> (sr));
            };
            ipcClient->connectAsync (ipcPipeName, 10000); // 10s: enough for Debug VST3 load
        }

        // VST3ロード（数秒かかる）。IPC接続はバックグラウンドで並行して進行する。
        mainWindow.reset (new MainWindow (getApplicationName(), pluginPath));
    }

    void timerCallback() override
    {
        if (ipcClient != nullptr)
            ipcClient->sendHeartbeat();
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
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        void preparePlugin (float sampleRate, int32_t bufferSize)
        {
            if (pluginInstance != nullptr)
            {
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

        juce::AudioPluginInstance* getPluginInstance() const noexcept
        {
            return pluginInstance.get();
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
                
                pluginInstance = formatManager.createPluginInstance (*types[0], 44100.0, 512, error);
            }
            else
            {
                juce::Logger::writeToLog ("No plugin types found via scan. Falling back to manual description...");
                
                juce::PluginDescription desc;
                desc.fileOrIdentifier = path;
                if (path.endsWithIgnoreCase (".vst3")) desc.pluginFormatName = "VST3";
                else desc.pluginFormatName = "VST";
                
                pluginInstance = formatManager.createPluginInstance (desc, 44100.0, 512, error);
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
};

START_JUCE_APPLICATION (LvhBridgeApplication)
