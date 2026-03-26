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
    Heartbeat     = 0x05,  // Bridge → Core, keepalive (prevents read timeout)
    WindowPos     = 0x06,  // bidirectional: Bridge→Core (reports bounds), Core→Bridge (sets bounds)
    RequestState  = 0x07,  // Core → Bridge: request plugin state dump
    StateData     = 0x08,  // Bridge → Core: plugin state binary payload
    SetState      = 0x09,  // Core → Bridge: restore plugin state from binary payload
    SetWindowTitle = 0x0A, // Core → Bridge: update the Bridge window title string
};

// Shared memory layout: header + audio I/O buffers (stereo, up to 4096 samples)
// Core writes audioIn + signals Bridge; Bridge processes + writes audioOut + signals Core.
struct SharedAudioLayout
{
    float   sampleRate   = 44100.0f;
    int32_t bufferSize   = 512;
    int32_t numChannels  = 2;
    int32_t padding      = 0;           // alignment
    float   audioIn [2][4096];          // Core → Bridge
    float   audioOut[2][4096];          // Bridge → Core
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
    juce::MemoryBlock makeHeartbeat();
    juce::MemoryBlock makeWindowPos   (int x, int y, int w, int h);
    juce::MemoryBlock makeRequestState();
    juce::MemoryBlock makeStateData      (const juce::MemoryBlock& stateBytes);
    juce::MemoryBlock makeSetState       (const juce::MemoryBlock& stateBytes);
    juce::MemoryBlock makeSetWindowTitle (const juce::String& title);

    IpcMessageType    getType             (const juce::MemoryBlock& data);
    juce::MidiMessage parseMidi           (const juce::MemoryBlock& data);
    void              parseWindowPos      (const juce::MemoryBlock& data, int& x, int& y, int& w, int& h);
    juce::MemoryBlock parseStateData      (const juce::MemoryBlock& data);  // for StateData and SetState
    juce::String      parseSetWindowTitle (const juce::MemoryBlock& data);
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
    bool sendMidi          (const juce::MidiMessage& msg);
    bool sendAudioConfig   (float sampleRate, int32_t bufferSize);
    bool sendShutdown();
    bool sendWindowPos     (int x, int y, int w, int h);
    bool sendRequestState();
    bool sendSetState      (const juce::MemoryBlock& stateBytes);
    bool sendWindowTitle   (const juce::String& title);

    std::function<void()>                        onConnected;
    std::function<void()>                        onDisconnected;
    std::function<void()>                        onHeartbeat;
    std::function<void(int,int,int,int)>         onWindowPosReceived;
    std::function<void(const juce::MemoryBlock&)> onStateReceived;

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
// SharedMemoryBuffer
//
// OS-level shared memory wrapper for audio data transfer.
// Core creates the region; Bridge opens it by name.
// Layout: [SharedAudioLayout header][float audio samples...]
// =====================================================================
class SharedMemoryBuffer
{
public:
    SharedMemoryBuffer() = default;
    ~SharedMemoryBuffer() { close(); }

    /** Core side: create a new named shared memory region. */
    bool create (const juce::String& name, size_t sizeBytes);

    /** Bridge side: open an existing named shared memory region. */
    bool open (const juce::String& name);

    void close();

    bool   isOpen()   const noexcept { return pBuf != nullptr; }
    void*  getData()  const noexcept { return pBuf; }
    size_t getSize()  const noexcept { return mapSize; }

    /** Typed access to the SharedAudioLayout header at the start of the region. */
    SharedAudioLayout* getLayout() const noexcept
    {
        return pBuf != nullptr ? static_cast<SharedAudioLayout*> (pBuf) : nullptr;
    }

    /** Generate a unique shared memory name for one Bridge instance. */
    static juce::String generateName()
    {
        return "LVHAudio" + juce::String (juce::Time::currentTimeMillis());
    }

    /** Total allocation size matches the fixed-size SharedAudioLayout struct. */
    static constexpr size_t kDefaultSize = sizeof (SharedAudioLayout);

private:
    void*  hMapFile = nullptr; // HANDLE on Windows (stored as void* to avoid windows.h in header)
    void*  pBuf     = nullptr;
    size_t mapSize  = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SharedMemoryBuffer)
};

// =====================================================================
// SyncEvents
//
// Windows Named Event pair for Core↔Bridge audio-loop synchronization.
//   evtRequest (_Req):  Core sets → Bridge starts processBlock
//   evtDone    (_Done): Bridge sets → Core reads output and continues
//
// Both are auto-reset events (WaitForSingleObject resets them automatically).
// Core creates; Bridge opens by the same baseName.
// =====================================================================
class SyncEvents
{
public:
    SyncEvents() = default;
    ~SyncEvents() { close(); }

    /** Core side: create both named events. */
    bool create (const juce::String& baseName);

    /** Bridge side: open existing named events. */
    bool open (const juce::String& baseName);

    void close();
    bool isOpen() const noexcept { return hRequest != nullptr && hEvtDone != nullptr; }

    /** Core → Bridge: signal Bridge to start processing. */
    void signalRequest() noexcept;

    /** Bridge: wait for Core's request (returns false on timeout). */
    bool waitForRequest (int timeoutMs) noexcept;

    /** Bridge → Core: signal Core that processing is done. */
    void signalDone() noexcept;

    /** Core: wait for Bridge's done signal (returns false on timeout). */
    bool waitForDone (int timeoutMs) noexcept;

    static juce::String generateName()
    {
        return "LVHSync" + juce::String (juce::Time::currentTimeMillis());
    }

private:
    void* hRequest  = nullptr; // evtRequestProcess (HANDLE as void*)
    void* hEvtDone  = nullptr; // evtProcessDone    (HANDLE as void*)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncEvents)
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

    /** Send a heartbeat to Core (call on a timer, e.g. every 1500 ms). */
    bool sendHeartbeat();
    bool sendWindowPos (int x, int y, int w, int h);
    bool sendStateData (const juce::MemoryBlock& stateBytes);

    std::function<void()>                           onConnected;
    std::function<void()>                           onDisconnected;
    std::function<void(const juce::MidiMessage&)>   onMidiReceived;
    std::function<void(float, int32_t)>             onAudioConfigReceived;
    std::function<void(int,int,int,int)>            onWindowPosReceived;
    std::function<void()>                           onRequestStateReceived;
    std::function<void(const juce::MemoryBlock&)>   onSetStateReceived;
    std::function<void(const juce::String&)>        onWindowTitleReceived;

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
