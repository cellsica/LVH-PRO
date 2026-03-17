#pragma once
#include <JuceHeader.h>

// =====================================================================
// IPC Protocol
// =====================================================================

enum class IpcMessageType : uint32_t
{
    Handshake   = 0x01,
    MidiData    = 0x02,
    AudioConfig = 0x03,
    Shutdown    = 0x04,
};

// Shared memory layout definition (for future audio data sharing via OS shared memory)
struct SharedAudioLayout
{
    float   sampleRate  = 44100.0f;
    int32_t bufferSize  = 512;
    int32_t numChannels = 2;
    // Audio samples follow in shared memory: numChannels * bufferSize * sizeof(float) bytes
};

// =====================================================================
// IpcProtocol: encode / decode MemoryBlock messages
//
// MemoryBlock layout:
//   [4 bytes: IpcMessageType (uint32_t LE)] [payload bytes...]
//
// MIDI payload:
//   [4 bytes: raw MIDI size (uint32_t LE)] [N bytes: raw MIDI data]
//
// AudioConfig payload:
//   [4 bytes: sampleRate (float LE)] [4 bytes: bufferSize (int32_t LE)]
// =====================================================================
namespace IpcProtocol
{
    juce::MemoryBlock makeHandshake();
    juce::MemoryBlock makeMidi        (const juce::MidiMessage& msg);
    juce::MemoryBlock makeAudioConfig (float sampleRate, int32_t bufferSize);
    juce::MemoryBlock makeShutdown();

    IpcMessageType    getType     (const juce::MemoryBlock& data);
    juce::MidiMessage parseMidi   (const juce::MemoryBlock& data);
}

// =====================================================================
// CoreIpcManager  (runs inside LVH-PRO / Core process)
//
// Acts as the named-pipe server. One instance per Bridge child process.
// Call startPipe() before launching Bridge, then use sendMidi() etc.
// =====================================================================
class CoreIpcManager : public juce::InterprocessConnection
{
public:
    CoreIpcManager();
    ~CoreIpcManager() override;

    /** Start listening on a named pipe (non-blocking; spawns background thread).
        Automatically stops any previous pipe first. */
    void startPipe (const juce::String& pipeName);
    void stopPipe();

    /** Thread-safe sends. Returns false if not yet connected. */
    bool sendMidi        (const juce::MidiMessage& msg);
    bool sendAudioConfig (float sampleRate, int32_t bufferSize);
    bool sendShutdown();

    std::function<void()> onConnected;
    std::function<void()> onDisconnected;

private:
    void connectionMade() override;
    void connectionLost() override;
    void messageReceived (const juce::MemoryBlock& message) override;

    struct ListenThread : public juce::Thread
    {
        ListenThread (CoreIpcManager& o, juce::String p)
            : Thread ("CoreIPC-Listen"), owner (o), pipeName (std::move (p)) {}
        void run() override;
        CoreIpcManager& owner;
        juce::String    pipeName;
    };
    std::unique_ptr<ListenThread> listenThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CoreIpcManager)
};

// =====================================================================
// BridgeIpcClient  (runs inside LVH-Bridge / child process)
//
// Acts as the named-pipe client. Connects to CoreIpcManager.
// Call connectAsync() with the pipe name received via --ipc-pipe arg.
// =====================================================================
class BridgeIpcClient : public juce::InterprocessConnection
{
public:
    BridgeIpcClient();
    ~BridgeIpcClient() override;

    /** Attempts to connect asynchronously (spawns background thread, retries for timeoutMs). */
    void connectAsync (const juce::String& pipeName, int timeoutMs = 5000);
    void disconnect();

    std::function<void()>                         onConnected;
    std::function<void()>                         onDisconnected;
    std::function<void(const juce::MidiMessage&)> onMidiReceived;

private:
    void connectionMade() override;
    void connectionLost() override;
    void messageReceived (const juce::MemoryBlock& message) override;

    struct ConnectThread : public juce::Thread
    {
        ConnectThread (BridgeIpcClient& o, juce::String p, int t)
            : Thread ("BridgeIPC-Connect"), owner (o), pipeName (std::move (p)), timeoutMs (t) {}
        void run() override;
        BridgeIpcClient& owner;
        juce::String     pipeName;
        int              timeoutMs;
    };
    std::unique_ptr<ConnectThread> connectThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeIpcClient)
};
