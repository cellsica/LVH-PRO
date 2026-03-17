#include <JuceHeader.h>
#include <memory>
#include "IPCManager.h"

// =====================================================================
// LVH-Bridge: プラグインホスト用の子プロセス
// =====================================================================
class LvhBridgeApplication : public juce::JUCEApplication
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
        auto args = juce::StringArray::fromTokens (commandLine, true);

        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--plugin" && i + 1 < args.size())
                pluginPath = args[i + 1].unquoted();
            else if (args[i] == "--ipc-pipe" && i + 1 < args.size())
                ipcPipeName = args[i + 1];
        }

        juce::Logger::writeToLog ("Detected Plugin Path: " + pluginPath);
        juce::Logger::writeToLog ("IPC Pipe Name: " + (ipcPipeName.isNotEmpty() ? ipcPipeName : "(none)"));

        // IPC接続を開始 (パイプ名が渡されていた場合)
        if (ipcPipeName.isNotEmpty())
        {
            ipcClient = std::make_unique<BridgeIpcClient>();
            ipcClient->onConnected    = [] { juce::Logger::writeToLog ("[Bridge] IPC channel ready."); };
            ipcClient->onDisconnected = [] { juce::Logger::writeToLog ("[Bridge] IPC channel closed."); };
            ipcClient->onMidiReceived = [] (const juce::MidiMessage& msg) {
                // Verification log: confirm MIDI arrives from Core
                juce::Logger::writeToLog ("[Bridge MIDI] " + msg.getDescription());
            };
            ipcClient->connectAsync (ipcPipeName, 5000);
        }

        mainWindow.reset (new MainWindow (getApplicationName(), pluginPath));
    }

    void shutdown() override
    {
        if (ipcClient != nullptr)
            ipcClient->disconnect();
        ipcClient.reset();
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

private:
    std::unique_ptr<juce::FileLogger>  fileLogger;
    std::unique_ptr<MainWindow>        mainWindow;
    std::unique_ptr<BridgeIpcClient>   ipcClient;
};

START_JUCE_APPLICATION (LvhBridgeApplication)
