#include "BridgeInstance.h"

BridgeInstance::BridgeInstance() {}

BridgeInstance::~BridgeInstance()
{
    shutdown();
}

bool BridgeInstance::launch (const juce::String& pluginPath, const juce::File& bridgeExe)
{
    if (! bridgeExe.existsAsFile())
    {
        juce::Logger::writeToLog ("[BridgeInstance] Error: bridge exe not found: "
                                  + bridgeExe.getFullPathName());
        return false;
    }

    pluginPath_ = pluginPath;
    state       = State::Connecting;

    // Generate unique names for this Bridge instance's IPC resources.
    const juce::String timestamp = juce::String (juce::Time::currentTimeMillis());
    juce::String pipeName = "LVH-Bridge-" + timestamp;
    juce::String shmName  = SharedMemoryBuffer::generateName();
    juce::String syncName = SyncEvents::generateName();

    // Start IPC pipe server (listens for the Bridge to connect).
    ipcManager.startPipe (pipeName);

    // Allocate shared audio memory.
    if (! sharedMem.create (shmName, SharedMemoryBuffer::kDefaultSize))
        juce::Logger::writeToLog ("[BridgeInstance] Warning: failed to create shared memory.");

    // Create named sync events for audio-loop synchronisation.
    if (! syncEvents.create (syncName))
        juce::Logger::writeToLog ("[BridgeInstance] Warning: failed to create sync events.");

    // Wire IPC callbacks.
    ipcManager.onConnected = [this] {
        state = State::Connected;
        juce::Logger::writeToLog ("[BridgeInstance] Connected: " + pluginPath_);
        if (onConnected) onConnected (this);
    };

    ipcManager.onDisconnected = [this] {
        state = State::Idle;
        juce::Logger::writeToLog ("[BridgeInstance] Disconnected: " + pluginPath_);
        if (onDisconnected) onDisconnected (this);
    };

    ipcManager.onWindowPosReceived = [this] (int x, int y, int w, int h) {
        lastWindowBounds = { x, y, w, h };
    };

    ipcManager.onStateReceived = [this] (const juce::MemoryBlock& state) {
        if (onStateReceived) onStateReceived (this, state);
    };

    // Launch the Bridge child process.
    juce::String args = "--plugin \"" + pluginPath + "\""
                      + " --ipc-pipe " + pipeName
                      + " --shm-name "  + shmName
                      + " --sync-name " + syncName;

    juce::Logger::writeToLog ("[BridgeInstance] Launching bridge: " + args);
    bridgeExe.startAsProcess (args);
    return true;
}

void BridgeInstance::shutdown()
{
    // Always clean up regardless of state.
    // NOTE: We do NOT call sendShutdown() here. If Bridge has already exited
    // (X button), Core's ConnectionThread is stuck in a reconnect loop waiting
    // for a new pipe client. In that state, pipe->write() calls connect(-1)
    // which blocks FOREVER (WaitForMultipleObjects with INFINITE timeout).
    // Instead, we call stopPipe() directly which signals the cancelEvent and
    // unblocks the ConnectionThread cleanly.
    // Bridge will detect the pipe closure via its own onDisconnected callback
    // and call systemRequestedQuit() to exit gracefully.
    juce::Logger::writeToLog ("[BridgeInstance] Shutting down: " + pluginPath_);
    ipcManager.stopPipe();     // signals cancelEvent → unblocks reconnect loop; joins threads
    syncEvents.close();
    sharedMem.close();
    state = State::Idle;
}

std::unique_ptr<BridgeSyncProcessor> BridgeInstance::createSyncProcessor()
{
    return std::make_unique<BridgeSyncProcessor> (sharedMem, syncEvents);
}

bool BridgeInstance::sendMidi (const juce::MidiMessage& msg)
{
    // Primary guard: state is set to Idle synchronously on the message thread
    // when the IPC disconnects. This covers the common case.
    if (state != State::Connected)
        return false;
    // Secondary guard: covers a brief race window where the JUCE ConnectionThread
    // has already exited (threadIsRunning=false) but the connectionLost message
    // has not yet been processed on the message thread (state still Connected).
    // In that window, pipe->write() with pipeReceiveMessageTimeout=-1 would block
    // the message thread permanently — this check prevents that.
    if (! ipcManager.isConnected())
        return false;
    return ipcManager.sendMidi (msg);
}

bool BridgeInstance::sendAudioConfig (float sampleRate, int32_t bufferSize)
{
    if (state != State::Connected)
        return false;
    return ipcManager.sendAudioConfig (sampleRate, bufferSize);
}

bool BridgeInstance::sendWindowPos (int x, int y, int w, int h)
{
    if (state != State::Connected) return false;
    return ipcManager.sendWindowPos (x, y, w, h);
}

bool BridgeInstance::sendRequestState()
{
    if (state != State::Connected) return false;
    return ipcManager.sendRequestState();
}

bool BridgeInstance::sendWindowTitle (const juce::String& title)
{
    if (state != State::Connected) return false;
    return ipcManager.sendWindowTitle (title);
}

bool BridgeInstance::sendSetState (const juce::MemoryBlock& stateBytes)
{
    if (state != State::Connected) return false;
    return ipcManager.sendSetState (stateBytes);
}
