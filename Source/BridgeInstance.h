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

    State             getState()      const noexcept { return state.load (std::memory_order_relaxed); }
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
    bool sendWindowTitle   (const juce::String& title);

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
    std::atomic<bool>  mixerMuted   { false };
    std::atomic<bool>  mixerSoloed  { false };
    std::atomic<bool>  mixerBypassed { false };  // Effect bypass (audio-thread safe)

    // ── Global Layer flag (message-thread only) ──────────────────────
    // Set by BridgeManager when launching bridges from a Global project (Slot 0).
    // Global bridges survive clearBridges(keepGlobal=true) when switching songs.
    bool isGlobal()         const noexcept { return isGlobal_; }
    void setIsGlobal (bool g) noexcept    { isGlobal_ = g; }

    // ── FX parent association (message-thread only) ──────────────────
    // For Role::Effect bridges only.
    // Empty        = master chain effect.
    // Non-empty    = per-channel effect; value is the parent instrument's pluginPath_.
    const juce::String& getFxParentPath() const noexcept { return fxParentPath_; }
    void setFxParentPath (const juce::String& path) { fxParentPath_ = path; }

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
    // Nested thread that sends MIDI to the Bridge process asynchronously,
    // so the JUCE message thread never blocks on a named-pipe write.
    class MidiSenderThread;

    // Fires on the message thread every 2 s; if no heartbeat for > 5 s while
    // Connected, calls stopPipe() to break the JUCE named-pipe reconnect loop
    // that would otherwise block any write call forever.
    class HeartbeatWatchdog : public juce::Timer
    {
    public:
        explicit HeartbeatWatchdog (BridgeInstance& owner) : owner_ (owner) {}
        void timerCallback() override;
    private:
        BridgeInstance& owner_;
    };

    CoreIpcManager       ipcManager;
    SharedMemoryBuffer   sharedMem;
    SyncEvents           syncEvents;
    juce::ChildProcess   childProcess_;   // Bridgeプロセスのハンドル (終了待機・強制終了用)
    std::atomic<State>   state           { State::Idle };
    Role                 role_           = Role::Instrument;
    juce::String         pluginPath_;
    juce::String         fxParentPath_;   // empty = master FX; non-empty = per-channel FX parent path
    juce::Rectangle<int> lastWindowBounds { 0, 0, 0, 0 };
    bool                                  isGlobal_        = false;
    std::unique_ptr<MidiSenderThread>     midiSender_;
    std::unique_ptr<HeartbeatWatchdog>    heartbeatWatchdog_;
    std::atomic<int64_t>                  lastHeartbeatMs_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeInstance)
};
