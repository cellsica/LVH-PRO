#pragma once
#include "IPCManager.h"
#include "BridgeSyncProcessor.h"

/**
 * @class BridgeInstance
 * @brief Core-side representation of one LVH-Bridge child process.
 *
 * Each loaded plugin runs in an isolated LVH-Bridge.exe subprocess.
 * BridgeInstance owns all Core-side OS resources needed to communicate
 * with that subprocess:
 *
 * - **CoreIpcManager** — named-pipe server that handles the IPC protocol
 *   (connect/disconnect, MIDI, state, window position, heartbeat).
 * - **SharedMemoryBuffer** — shared memory region for lock-free audio data
 *   transfer between the Core and Bridge audio threads.
 * - **SyncEvents** — a pair of named Win32 events used to synchronise the
 *   Core and Bridge audio loops frame-by-frame.
 *
 * **Typical usage:**
 * @code
 * auto bridge = std::make_unique<BridgeInstance>();
 * bridge->onConnected    = [](BridgeInstance* b) { ... };
 * bridge->onDisconnected = [](BridgeInstance* b) { ... };
 * bridge->launch(pluginPath, bridgeExeFile);
 * @endcode
 *
 * **Lifetime constraint:**
 * createSyncProcessor() returns a BridgeSyncProcessor whose internals hold
 * *references* into this object's SHM and SyncEvents.  This BridgeInstance
 * must therefore outlive any processor (and its containing audio graph node)
 * created via createSyncProcessor().
 *
 * **Thread safety:**
 * - onConnected / onDisconnected are invoked on the IPC listener thread.
 *   Use MessageManager::callAsync for any work that touches the audio graph or UI.
 * - mixerGain, mixerPan, mixerMuted, mixerBypassed are atomics: written by
 *   the message thread (MixerStrip) and read by the audio thread.
 * - peakL / peakR are atomics: written by the audio thread, read-and-reset
 *   by the UI timer on the message thread.
 * - All other members are message-thread only.
 */
class BridgeInstance
{
public:
    /** @brief Subprocess connection state. */
    enum class State { Idle, Connecting, Connected };

    /** @brief Role of this bridge in the audio graph. */
    enum class Role  { Instrument, Effect };

    BridgeInstance();
    ~BridgeInstance();

    /**
     * @brief Launch an LVH-Bridge.exe subprocess for the given plugin.
     *
     * Creates the named-pipe server, shared memory region, and sync events,
     * then spawns the child process with the plugin path as an argument.
     *
     * @param pluginPath  Absolute path to the VST3/CLAP plugin file.
     * @param bridgeExe   Path to LVH-Bridge.exe.
     * @return            true if the subprocess was started successfully.
     *                    false if @p bridgeExe does not exist.
     */
    bool launch (const juce::String& pluginPath, const juce::File& bridgeExe);

    /**
     * @brief Gracefully terminate the Bridge subprocess.
     *
     * Sends a Shutdown IPC message, stops the pipe server, and closes all OS handles.
     * Safe to call at any State; no-op if already Idle.
     */
    void shutdown();

    /** @brief Returns the current connection state. */
    State getState() const noexcept { return state.load (std::memory_order_relaxed); }

    /** @brief Returns the audio graph role of this bridge. */
    Role getRole() const noexcept { return role_; }

    /** @brief Set the audio graph role. Must be called before adding to the graph. */
    void setRole (Role r) noexcept { role_ = r; }

    /** @brief Returns the absolute path to the loaded plugin file. */
    const juce::String& getPluginPath() const noexcept { return pluginPath_; }

    /**
     * @brief Returns a reference to this bridge's shared audio memory.
     *
     * Used by MultiSourceBridgeProcessor and BridgeEffectProcessor to
     * read the audio buffer produced by the Bridge subprocess.
     * @return Reference to the SharedMemoryBuffer (valid for the lifetime of this object).
     */
    SharedMemoryBuffer& getSharedMemory() noexcept { return sharedMem; }

    /**
     * @brief Returns a reference to this bridge's audio-loop sync events.
     *
     * The Core audio thread signals the "render" event each block;
     * the Bridge audio thread signals the "done" event when it has written
     * the output buffer into shared memory.
     * @return Reference to the SyncEvents pair.
     */
    SyncEvents& getSyncEvents() noexcept { return syncEvents; }

    /**
     * @brief Create a BridgeSyncProcessor for single-bridge graph configurations.
     *
     * Used by AudioEngine::buildGraphWithBridgeSync() for the legacy single-plugin mode.
     * The returned processor holds references into this object's SHM and SyncEvents —
     * this BridgeInstance must outlive the processor and its audio graph node.
     *
     * @return A new BridgeSyncProcessor instance.
     */
    std::unique_ptr<BridgeSyncProcessor> createSyncProcessor();

    // ── IPC send helpers (message thread) ────────────────────────────────────
    // All methods are no-ops and return false if the bridge is not Connected.

    /**
     * @brief Send a MIDI message to the Bridge subprocess.
     *
     * The message is queued to a MidiSenderThread so the message thread
     * never blocks on a named-pipe write.
     *
     * @param msg  The MIDI message to send.
     * @return     true if queued successfully.
     */
    bool sendMidi (const juce::MidiMessage& msg);

    /**
     * @brief Notify the Bridge of the current audio device configuration.
     * @param sampleRate  Current sample rate in Hz.
     * @param bufferSize  Current audio buffer size in samples.
     * @return            true if sent successfully.
     */
    bool sendAudioConfig (float sampleRate, int32_t bufferSize);

    /**
     * @brief Tell the Bridge where to position its plugin editor window.
     * @param x  Screen X position.
     * @param y  Screen Y position.
     * @param w  Window width in pixels.
     * @param h  Window height in pixels.
     * @return   true if sent successfully.
     */
    bool sendWindowPos (int x, int y, int w, int h);

    /**
     * @brief Request the Bridge to send back its current plugin state.
     *
     * The response arrives asynchronously via onStateReceived.
     * @return true if the request was sent.
     */
    bool sendRequestState();

    /**
     * @brief Push a serialized plugin state to the Bridge for restoration.
     * @param stateBytes  Raw plugin state data (from AudioProcessor::getStateInformation).
     * @return            true if sent successfully.
     */
    bool sendSetState (const juce::MemoryBlock& stateBytes);

    /**
     * @brief Set the title bar text of the Bridge's plugin editor window.
     * @param title  New title string.
     * @return       true if sent successfully.
     */
    bool sendWindowTitle (const juce::String& title);

    /**
     * @brief Returns the last known screen bounds of the Bridge's plugin editor.
     * @return Rectangle in screen coordinates, or a zero rectangle if unknown.
     */
    juce::Rectangle<int> getWindowBounds() const noexcept { return lastWindowBounds; }

    /**
     * @brief Fired when the Bridge sends back its plugin state (response to sendRequestState()).
     *
     * Called on the IPC listener thread — use MessageManager::callAsync if touching the UI.
     */
    std::function<void(BridgeInstance*, const juce::MemoryBlock&)> onStateReceived;

    /**
     * @brief Fired when the Bridge subprocess connects and is ready.
     *
     * Called on the IPC listener thread — use MessageManager::callAsync for
     * any work that modifies the audio graph or updates the UI.
     */
    std::function<void(BridgeInstance*)> onConnected;

    /**
     * @brief Fired when the Bridge subprocess disconnects or crashes.
     *
     * Called on the IPC listener thread — use MessageManager::callAsync for
     * any work that modifies the audio graph or updates the UI.
     */
    std::function<void(BridgeInstance*)> onDisconnected;

    // ── Mixer state (audio-thread safe) ───────────────────────────────────────

    std::atomic<float> mixerGain    { 1.0f };   ///< Channel gain, linear [0.0, 1.5]. Written: message thread. Read: audio thread.
    std::atomic<float> mixerPan     { 0.0f };   ///< Pan position [-1.0 L … 0.0 C … +1.0 R]. Written: message thread. Read: audio thread.
    std::atomic<bool>  mixerMuted   { false };  ///< Channel mute. Written: message thread. Read: audio thread.
    std::atomic<bool>  mixerSoloed  { false };  ///< Channel solo. Written: message thread. Read: audio thread.
    std::atomic<bool>  mixerBypassed { false }; ///< Plugin bypass for Effect bridges. Written: message thread. Read: audio thread.

    // ── Global Layer flag (message thread only) ───────────────────────────────

    /**
     * @brief Returns true if this bridge belongs to the Global layer (Stage Set Slot 0).
     *
     * Global bridges survive BridgeManager::clearBridges(keepGlobal=true),
     * allowing master FX and audio settings to persist when switching songs.
     */
    bool isGlobal()           const noexcept { return isGlobal_; }

    /** @brief Mark this bridge as belonging to the Global layer. */
    void setIsGlobal (bool g) noexcept       { isGlobal_ = g; }

    // ── FX parent association (message thread only) ───────────────────────────

    /**
     * @brief Returns the parent instrument's plugin path for per-channel FX bridges.
     *
     * Empty string means this is a master chain effect.
     * Non-empty means this Effect bridge is chained to the named instrument.
     */
    const juce::String& getFxParentPath() const noexcept { return fxParentPath_; }

    /**
     * @brief Set the parent instrument path for a per-channel FX bridge.
     * @param path  Absolute path of the parent instrument plugin, or empty for master FX.
     */
    void setFxParentPath (const juce::String& path) { fxParentPath_ = path; }

    // ── Mixer UI customisation (message thread only) ──────────────────────────

    juce::String mixerCustomName;                                       ///< User label for this channel strip. Empty = use plugin filename.
    juce::Colour mixerCustomColor { juce::Colours::transparentBlack };  ///< Strip colour. Transparent = use palette default.

    // ── Per-channel peak meters (cross-thread) ────────────────────────────────

    std::atomic<float> peakL { 0.0f };  ///< Left-channel peak. Written: audio thread. Read/reset: message thread.
    std::atomic<float> peakR { 0.0f };  ///< Right-channel peak. Written: audio thread. Read/reset: message thread.

    /**
     * @brief Atomically read and reset the left-channel peak.
     *
     * Call from the message thread only (e.g. from a UI repaint timer).
     * @return Peak magnitude since the last call.
     */
    float exchangePeakL() noexcept { return peakL.exchange (0.f, std::memory_order_relaxed); }

    /**
     * @brief Atomically read and reset the right-channel peak.
     *
     * Call from the message thread only.
     * @return Peak magnitude since the last call.
     */
    float exchangePeakR() noexcept { return peakR.exchange (0.f, std::memory_order_relaxed); }

private:
    /// Queues all IPC messages and sends them to the Bridge pipe asynchronously,
    /// preventing the message thread from blocking on named-pipe writes.
    class IpcSenderThread;

    /// Fires every 2 s on the message thread.  If no heartbeat has been received
    /// for more than 5 s while Connected, stops the pipe to break the JUCE
    /// named-pipe reconnect loop that would otherwise block write calls indefinitely.
    class HeartbeatWatchdog : public juce::Timer
    {
    public:
        explicit HeartbeatWatchdog (BridgeInstance& owner) : owner_ (owner) {}
        void timerCallback() override;
    private:
        BridgeInstance& owner_;
    };

    CoreIpcManager     ipcManager;
    SharedMemoryBuffer sharedMem;
    SyncEvents         syncEvents;
    juce::ChildProcess childProcess_;      ///< Handle to the Bridge subprocess (for termination).

    std::atomic<State> state        { State::Idle };
    Role               role_        = Role::Instrument;
    juce::String       pluginPath_;
    juce::String       fxParentPath_;
    juce::Rectangle<int> lastWindowBounds { 0, 0, 0, 0 };
    bool               isGlobal_    = false;

    std::unique_ptr<IpcSenderThread>   ipcSender_;
    std::unique_ptr<HeartbeatWatchdog> heartbeatWatchdog_;
    std::atomic<int64_t>               lastHeartbeatMs_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeInstance)
};
