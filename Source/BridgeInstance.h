#pragma once
#include "IPCManager.h"
#include "BridgeSyncProcessor.h"

// =====================================================================
// BridgeInstance
//
// Encapsulates all Core-side resources for one LVH-Bridge child process:
//   - CoreIpcManager  (named pipe server)
//   - SharedMemoryBuffer (shared audio memory)
//   - SyncEvents (named event pair for audio-loop synchronisation)
//
// Usage:
//   auto bridge = std::make_unique<BridgeInstance>();
//   bridge->onConnected    = [](BridgeInstance* b) { ... };
//   bridge->onDisconnected = [](BridgeInstance* b) { ... };
//   bridge->launch(pluginPath, bridgeExeFile);
//
// NOTE: createSyncProcessor() returns a processor whose internals hold
// *references* into this BridgeInstance's SHM/SyncEvents, so this
// BridgeInstance must outlive the processor and its containing graph.
// =====================================================================
class BridgeInstance
{
public:
    enum class State { Idle, Connecting, Connected };

    BridgeInstance();
    ~BridgeInstance();

    // Launch LVH-Bridge.exe for the given plugin path.
    // Creates the IPC pipe server, shared memory, and sync events,
    // then spawns the child process with the appropriate arguments.
    // Returns false if bridgeExe is not found.
    bool launch (const juce::String& pluginPath, const juce::File& bridgeExe);

    // Gracefully shut down: send Shutdown message, stop the pipe, close OS handles.
    void shutdown();

    State             getState()      const noexcept { return state; }
    const juce::String& getPluginPath() const noexcept { return pluginPath_; }

    // Direct access for MultiSourceBridgeProcessor construction.
    SharedMemoryBuffer& getSharedMemory() noexcept { return sharedMem; }
    SyncEvents&         getSyncEvents()   noexcept { return syncEvents; }

    // Create a BridgeSyncProcessor for this bridge's SHM and SyncEvents.
    // The caller is responsible for ensuring this BridgeInstance outlives
    // any processor (and the graph node) created by this method.
    std::unique_ptr<BridgeSyncProcessor> createSyncProcessor();

    // IPC send helpers — no-ops if not connected.
    bool sendMidi        (const juce::MidiMessage& msg);
    bool sendAudioConfig (float sampleRate, int32_t bufferSize);

    // Callbacks — must be set before calling launch().
    // These are invoked on the IPC listener thread; use MessageManager::callAsync
    // for any work that touches the audio graph or UI.
    std::function<void(BridgeInstance*)> onConnected;
    std::function<void(BridgeInstance*)> onDisconnected;

private:
    CoreIpcManager     ipcManager;
    SharedMemoryBuffer sharedMem;
    SyncEvents         syncEvents;
    State              state     = State::Idle;
    juce::String       pluginPath_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeInstance)
};
