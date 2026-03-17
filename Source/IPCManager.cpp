#include "IPCManager.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

// =====================================================================
// IpcProtocol helpers
// =====================================================================

static juce::MemoryBlock makeRawMessage (IpcMessageType type, const void* payload, size_t payloadSize)
{
    juce::MemoryBlock block (4 + payloadSize);
    auto t = static_cast<uint32_t> (type);
    std::memcpy (block.getData(), &t, 4);
    if (payloadSize > 0 && payload != nullptr)
        std::memcpy (static_cast<uint8_t*> (block.getData()) + 4, payload, payloadSize);
    return block;
}

juce::MemoryBlock IpcProtocol::makeHandshake()
{
    const char payload[] = "HELLO";
    return makeRawMessage (IpcMessageType::Handshake, payload, sizeof (payload) - 1);
}

juce::MemoryBlock IpcProtocol::makeMidi (const juce::MidiMessage& msg)
{
    const uint8_t* raw      = msg.getRawData();
    auto           rawSize  = static_cast<uint32_t> (msg.getRawDataSize());

    // payload: [4 bytes: raw MIDI size][N bytes: raw MIDI data]
    juce::MemoryBlock payload (4 + rawSize);
    std::memcpy (payload.getData(), &rawSize, 4);
    std::memcpy (static_cast<uint8_t*> (payload.getData()) + 4, raw, rawSize);

    return makeRawMessage (IpcMessageType::MidiData, payload.getData(), payload.getSize());
}

juce::MemoryBlock IpcProtocol::makeAudioConfig (float sampleRate, int32_t bufferSize)
{
    struct Payload { float sr; int32_t buf; };
    Payload p { sampleRate, bufferSize };
    return makeRawMessage (IpcMessageType::AudioConfig, &p, sizeof (p));
}

juce::MemoryBlock IpcProtocol::makeShutdown()
{
    return makeRawMessage (IpcMessageType::Shutdown, nullptr, 0);
}

IpcMessageType IpcProtocol::getType (const juce::MemoryBlock& data)
{
    if (data.getSize() < 4)
        return static_cast<IpcMessageType> (0);
    uint32_t t = 0;
    std::memcpy (&t, data.getData(), 4);
    return static_cast<IpcMessageType> (t);
}

juce::MidiMessage IpcProtocol::parseMidi (const juce::MemoryBlock& data)
{
    // layout: [4 bytes type][4 bytes raw size][N bytes raw data]
    if (data.getSize() < 8)
        return {};

    const auto* ptr = static_cast<const uint8_t*> (data.getData());
    ptr += 4; // skip type field

    uint32_t rawSize = 0;
    std::memcpy (&rawSize, ptr, 4);
    ptr += 4;

    if (data.getSize() < 8 + rawSize || rawSize == 0)
        return {};

    return juce::MidiMessage (ptr, static_cast<int> (rawSize));
}

// =====================================================================
// SharedMemoryBuffer
// =====================================================================

bool SharedMemoryBuffer::create (const juce::String& name, size_t sizeBytes)
{
    close();
#if JUCE_WINDOWS
    HANDLE h = CreateFileMappingW (
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD> (sizeBytes),
        name.toWideCharPointer());

    if (h == nullptr)
    {
        juce::Logger::writeToLog ("[SharedMem] CreateFileMapping failed: " + juce::String ((int)GetLastError()));
        return false;
    }

    void* view = MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, sizeBytes);
    if (view == nullptr)
    {
        juce::Logger::writeToLog ("[SharedMem] MapViewOfFile failed: " + juce::String ((int)GetLastError()));
        CloseHandle (h);
        return false;
    }

    hMapFile = h;
    pBuf     = view;
    mapSize  = sizeBytes;
    std::memset (pBuf, 0, sizeBytes);
    juce::Logger::writeToLog ("[SharedMem] Created: " + name + " (" + juce::String ((int)sizeBytes) + " bytes)");
    return true;
#else
    juce::ignoreUnused (name, sizeBytes);
    juce::Logger::writeToLog ("[SharedMem] create() not implemented on this platform.");
    return false;
#endif
}

bool SharedMemoryBuffer::open (const juce::String& name)
{
    close();
#if JUCE_WINDOWS
    HANDLE h = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, name.toWideCharPointer());
    if (h == nullptr)
    {
        juce::Logger::writeToLog ("[SharedMem] OpenFileMapping failed: " + juce::String ((int)GetLastError()));
        return false;
    }

    void* view = MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (view == nullptr)
    {
        juce::Logger::writeToLog ("[SharedMem] MapViewOfFile (open) failed: " + juce::String ((int)GetLastError()));
        CloseHandle (h);
        return false;
    }

    hMapFile = h;
    pBuf     = view;

    MEMORY_BASIC_INFORMATION mbi {};
    VirtualQuery (pBuf, &mbi, sizeof (mbi));
    mapSize = mbi.RegionSize;
    juce::Logger::writeToLog ("[SharedMem] Opened: " + name + " (" + juce::String ((int)mapSize) + " bytes)");
    return true;
#else
    juce::ignoreUnused (name);
    juce::Logger::writeToLog ("[SharedMem] open() not implemented on this platform.");
    return false;
#endif
}

void SharedMemoryBuffer::close()
{
#if JUCE_WINDOWS
    if (pBuf     != nullptr) { UnmapViewOfFile (pBuf);              pBuf     = nullptr; }
    if (hMapFile != nullptr) { CloseHandle (static_cast<HANDLE>(hMapFile)); hMapFile = nullptr; }
#endif
    mapSize = 0;
}

// =====================================================================
// SyncEvents
// =====================================================================

bool SyncEvents::create (const juce::String& baseName)
{
    close();
#if JUCE_WINDOWS
    auto reqName  = baseName + "_Req";
    auto doneName = baseName + "_Done";

    HANDLE hEvtReq  = CreateEventW (nullptr, FALSE, FALSE, reqName.toWideCharPointer());
    HANDLE hEvtDn   = CreateEventW (nullptr, FALSE, FALSE, doneName.toWideCharPointer());

    if (hEvtReq == nullptr || hEvtDn == nullptr)
    {
        juce::Logger::writeToLog ("[SyncEvents] CreateEvent failed: " + juce::String ((int)GetLastError()));
        if (hEvtReq) CloseHandle (hEvtReq);
        if (hEvtDn)  CloseHandle (hEvtDn);
        return false;
    }

    hRequest = hEvtReq;
    hEvtDone = hEvtDn;
    juce::Logger::writeToLog ("[SyncEvents] Created: " + baseName);
    return true;
#else
    juce::ignoreUnused (baseName);
    return false;
#endif
}

bool SyncEvents::open (const juce::String& baseName)
{
    close();
#if JUCE_WINDOWS
    auto reqName  = baseName + "_Req";
    auto doneName = baseName + "_Done";

    HANDLE hEvtReq = OpenEventW (EVENT_ALL_ACCESS, FALSE, reqName.toWideCharPointer());
    HANDLE hEvtDn  = OpenEventW (EVENT_ALL_ACCESS, FALSE, doneName.toWideCharPointer());

    if (hEvtReq == nullptr || hEvtDn == nullptr)
    {
        juce::Logger::writeToLog ("[SyncEvents] OpenEvent failed: " + juce::String ((int)GetLastError()));
        if (hEvtReq) CloseHandle (hEvtReq);
        if (hEvtDn)  CloseHandle (hEvtDn);
        return false;
    }

    hRequest = hEvtReq;
    hEvtDone = hEvtDn;
    juce::Logger::writeToLog ("[SyncEvents] Opened: " + baseName);
    return true;
#else
    juce::ignoreUnused (baseName);
    return false;
#endif
}

void SyncEvents::close()
{
#if JUCE_WINDOWS
    if (hRequest != nullptr) { CloseHandle (static_cast<HANDLE> (hRequest)); hRequest = nullptr; }
    if (hEvtDone != nullptr) { CloseHandle (static_cast<HANDLE> (hEvtDone)); hEvtDone = nullptr; }
#endif
}

void SyncEvents::signalRequest() noexcept
{
#if JUCE_WINDOWS
    if (hRequest) SetEvent (static_cast<HANDLE> (hRequest));
#endif
}

bool SyncEvents::waitForRequest (int timeoutMs) noexcept
{
#if JUCE_WINDOWS
    if (!hRequest) return false;
    return WaitForSingleObject (static_cast<HANDLE> (hRequest), static_cast<DWORD> (timeoutMs)) == WAIT_OBJECT_0;
#else
    juce::ignoreUnused (timeoutMs);
    return false;
#endif
}

void SyncEvents::signalDone() noexcept
{
#if JUCE_WINDOWS
    if (hEvtDone) SetEvent (static_cast<HANDLE> (hEvtDone));
#endif
}

bool SyncEvents::waitForDone (int timeoutMs) noexcept
{
#if JUCE_WINDOWS
    if (!hEvtDone) return false;
    return WaitForSingleObject (static_cast<HANDLE> (hEvtDone), static_cast<DWORD> (timeoutMs)) == WAIT_OBJECT_0;
#else
    juce::ignoreUnused (timeoutMs);
    return false;
#endif
}

// =====================================================================
// CoreIpcManager
// =====================================================================

CoreIpcManager::CoreIpcManager()
    : juce::InterprocessConnection (true /* callbacks on message thread */, 0xAB12CD34)
{
}

CoreIpcManager::~CoreIpcManager()
{
    stopPipe();
}

void CoreIpcManager::startPipe (const juce::String& pipeName)
{
    stopPipe();
    listenThread = std::make_unique<ListenThread> (*this, pipeName);
    listenThread->startThread();
}

void CoreIpcManager::stopPipe()
{
    disconnect();
    if (listenThread != nullptr)
    {
        listenThread->signalThreadShouldExit();
        listenThread->stopThread (2000);
        listenThread.reset();
    }
}

bool CoreIpcManager::sendMidi (const juce::MidiMessage& msg)
{
    return sendMessage (IpcProtocol::makeMidi (msg));
}

bool CoreIpcManager::sendAudioConfig (float sampleRate, int32_t bufferSize)
{
    return sendMessage (IpcProtocol::makeAudioConfig (sampleRate, bufferSize));
}

bool CoreIpcManager::sendShutdown()
{
    return sendMessage (IpcProtocol::makeShutdown());
}

void CoreIpcManager::connectionMade()
{
    juce::Logger::writeToLog ("[Core IPC] Bridge connected. Sending handshake.");
    sendMessage (IpcProtocol::makeHandshake());
    if (onConnected) onConnected();
}

void CoreIpcManager::connectionLost()
{
    juce::Logger::writeToLog ("[Core IPC] Bridge disconnected.");
    if (onDisconnected) onDisconnected();
}

void CoreIpcManager::messageReceived (const juce::MemoryBlock& message)
{
    // Core does not currently expect messages from Bridge
    juce::Logger::writeToLog ("[Core IPC] Unexpected message from Bridge, type="
                              + juce::String (static_cast<uint32_t> (IpcProtocol::getType (message))));
}

void CoreIpcManager::ListenThread::run()
{
    juce::Logger::writeToLog ("[Core IPC] Waiting for Bridge on pipe: " + pipeName);
    // createPipe blocks until a client connects or disconnect() is called
    bool ok = owner.createPipe (pipeName, -1 /* no receive timeout */);
    if (! ok)
        juce::Logger::writeToLog ("[Core IPC] createPipe failed or was cancelled for: " + pipeName);
}

// =====================================================================
// BridgeIpcClient
// =====================================================================

BridgeIpcClient::BridgeIpcClient()
    : juce::InterprocessConnection (true /* callbacks on message thread */, 0xAB12CD34)
{
}

BridgeIpcClient::~BridgeIpcClient()
{
    disconnect();
    if (connectThread != nullptr)
    {
        connectThread->signalThreadShouldExit();
        connectThread->stopThread (2000);
    }
}

void BridgeIpcClient::connectAsync (const juce::String& pipeName, int timeoutMs)
{
    connectThread = std::make_unique<ConnectThread> (*this, pipeName, timeoutMs);
    connectThread->startThread();
}

void BridgeIpcClient::disconnect()
{
    juce::InterprocessConnection::disconnect();
}

void BridgeIpcClient::connectionMade()
{
    juce::Logger::writeToLog ("[Bridge IPC] Connected to Core.");
    if (onConnected) onConnected();
}

void BridgeIpcClient::connectionLost()
{
    juce::Logger::writeToLog ("[Bridge IPC] Disconnected from Core.");
    if (onDisconnected) onDisconnected();
}

void BridgeIpcClient::messageReceived (const juce::MemoryBlock& message)
{
    auto type = IpcProtocol::getType (message);

    switch (type)
    {
        case IpcMessageType::Handshake:
            juce::Logger::writeToLog ("[Bridge IPC] Handshake received from Core. IPC ready.");
            break;

        case IpcMessageType::MidiData:
        {
            auto midi = IpcProtocol::parseMidi (message);
            juce::Logger::writeToLog ("[Bridge IPC] MIDI received: "
                                      + midi.getDescription()
                                      + " (" + juce::String (midi.getRawDataSize()) + " bytes)");
            if (onMidiReceived)
                onMidiReceived (midi);
            break;
        }

        case IpcMessageType::AudioConfig:
        {
            if (message.getSize() >= 4 + 8)
            {
                float   sr  = 0.0f;
                int32_t buf = 0;
                const auto* ptr = static_cast<const uint8_t*> (message.getData()) + 4;
                std::memcpy (&sr,  ptr,     4);
                std::memcpy (&buf, ptr + 4, 4);
                juce::Logger::writeToLog ("[Bridge IPC] AudioConfig received: SR="
                                          + juce::String (sr, 1)
                                          + " BS=" + juce::String (buf));
                if (onAudioConfigReceived)
                    onAudioConfigReceived (sr, buf);
            }
            break;
        }

        case IpcMessageType::Shutdown:
            juce::Logger::writeToLog ("[Bridge IPC] Shutdown requested by Core.");
            juce::MessageManager::callAsync ([] {
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            });
            break;

        default:
            juce::Logger::writeToLog ("[Bridge IPC] Unknown message type: "
                                      + juce::String (static_cast<uint32_t> (type)));
            break;
    }
}

void BridgeIpcClient::ConnectThread::run()
{
    juce::Logger::writeToLog ("[Bridge IPC] Attempting to connect to pipe: " + pipeName);

    const int retryIntervalMs = 200;
    int elapsed = 0;

    while (! threadShouldExit() && elapsed < timeoutMs)
    {
        // connectToPipe returns true on success; false if pipe doesn't exist yet
        if (owner.connectToPipe (pipeName, -1 /* no receive timeout */))
        {
            juce::Logger::writeToLog ("[Bridge IPC] Connection established after "
                                      + juce::String (elapsed) + "ms.");
            return;
        }
        juce::Thread::sleep (retryIntervalMs);
        elapsed += retryIntervalMs;
    }

    juce::Logger::writeToLog ("[Bridge IPC] Connection timed out after "
                              + juce::String (elapsed) + "ms.");
}
