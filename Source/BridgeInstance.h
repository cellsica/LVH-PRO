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
    enum class Role  { Instrument, Effect };

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
    Role              getRole()       const noexcept { return role_; }
    void              setRole (Role r)      noexcept { role_ = r; }
    const juce::String& getPluginPath() const noexcept { return pluginPath_; }

    // Direct access for MultiSourceBridgeProcessor construction.
    SharedMemoryBuffer& getSharedMemory() noexcept { return sharedMem; }
    SyncEvents&         getSyncEvents()   noexcept { return syncEvents; }

    // Create a BridgeSyncProcessor for this bridge's SHM and SyncEvents.
    // The caller is responsible for ensuring this BridgeInstance outlives
    // any processor (and the graph node) created by this method.
    std::unique_ptr<BridgeSyncProcessor> createSyncProcessor();

    // IPC send helpers — no-ops if not connected.
    bool sendMidi          (const juce::MidiMessage& msg);
    bool sendAudioConfig   (float sampleRate, int32_t bufferSize);
    bool sendWindowPos     (int x, int y, int w, int h);
    bool sendRequestState();
    bool sendSetState      (const juce::MemoryBlock& stateBytes);

    juce::Rectangle<int> getWindowBounds() const noexcept { return lastWindowBounds; }

    // Called when Bridge reports its plugin state (response to sendRequestState)
    std::function<void(BridgeInstance*, const juce::MemoryBlock&)> onStateReceived;

    // Callbacks — must be set before calling launch().
    // These are invoked on the IPC listener thread; use MessageManager::callAsync
    // for any work that touches the audio graph or UI.
    std::function<void(BridgeInstance*)> onConnected;
    std::function<void(BridgeInstance*)> onDisconnected;

    // ── Mixer state (audio-thread safe) ──────────────────────────────
    // Written by the message thread (MixerStrip callbacks).
    // Read by the audio thread (MultiSourceBridgeProcessor::processBlock).
    std::atomic<float> mixerGain   { 1.0f };  // linear, 0.0 – 1.5
    std::atomic<float> mixerPan    { 0.0f };  // -1.0 (L) … 0.0 (C) … +1.0 (R)
    std::atomic<bool>  mixerMuted  { false };
    std::atomic<bool>  mixerSoloed { false };

    // ── Mixer UI customization (message-thread only) ──────────────────
    juce::String mixerCustomName;                                   // empty = use plugin filename
    juce::Colour mixerCustomColor { juce::Colours::transparentBlack }; // transparent = use palette default

    // Written by the audio thread, read-and-reset by the UI timer.
    std::atomic<float> peakL { 0.0f };
    std::atomic<float> peakR { 0.0f };

    // Call from the message thread only.  Returns the peak since the last
    // call and resets the stored value to 0 atomically.
    float exchangePeakL() noexcept { return peakL.exchange (0.f, std::memory_order_relaxed); }
    float exchangePeakR() noexcept { return peakR.exchange (0.f, std::memory_order_relaxed); }

private:
    CoreIpcManager       ipcManager;
    SharedMemoryBuffer   sharedMem;
    SyncEvents           syncEvents;
    State                state           = State::Idle;
    Role                 role_           = Role::Instrument;
    juce::String         pluginPath_;
    juce::Rectangle<int> lastWindowBounds { 0, 0, 0, 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeInstance)
};
