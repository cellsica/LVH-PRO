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
    // If Bridge disconnected naturally (state == Idle), ipcManager.stopPipe()
    // must still be called to join the internal IPC receive thread — without it
    // the thread remains alive and blocks CoreIpcManager's destructor.
    juce::Logger::writeToLog ("[BridgeInstance] Shutting down: " + pluginPath_);
    ipcManager.sendShutdown(); // no-op if not connected
    ipcManager.stopPipe();     // joins listen thread + internal receive thread
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
    return ipcManager.sendMidi (msg);
}

bool BridgeInstance::sendAudioConfig (float sampleRate, int32_t bufferSize)
{
    return ipcManager.sendAudioConfig (sampleRate, bufferSize);
}
