#include "BridgeInstance.h"

// =====================================================================
// IpcSenderThread
//
// Sends all IPC messages to the Bridge process on a dedicated thread so
// that named-pipe writes never block the JUCE message thread.
// The message thread calls enqueue() (non-blocking); this thread does
// the actual ipcManager send calls which may stall briefly on a slow pipe.
// =====================================================================
class BridgeInstance::IpcSenderThread : public juce::Thread
{
public:
    // ── Message item types ────────────────────────────────────────────
    struct MidiItem        { juce::MidiMessage msg; };
    struct WindowPosItem   { int x, y, w, h; };
    struct WindowTitleItem { juce::String title; };
    struct RequestStateItem{};
    struct SetStateItem    { juce::MemoryBlock data; };
    struct AudioConfigItem { float sampleRate; int32_t bufferSize; };

    using Item = std::variant<MidiItem, WindowPosItem, WindowTitleItem,
                              RequestStateItem, SetStateItem, AudioConfigItem>;

    IpcSenderThread (BridgeInstance& owner)
        : juce::Thread ("BridgeIpcSender"), owner_ (owner) {}

    // Thread-safe, non-blocking enqueue from the message thread.
    void enqueue (Item item)
    {
        {
            juce::ScopedLock sl (lock_);
            queue_.push_back (std::move (item));
        }
        event_.signal();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            event_.wait (100);

            // Swap under lock to minimise lock-hold time during pipe writes.
            std::vector<Item> toSend;
            {
                juce::ScopedLock sl (lock_);
                toSend.swap (queue_);
            }

            for (auto& item : toSend)
            {
                if (owner_.state.load (std::memory_order_acquire) != BridgeInstance::State::Connected
                    || ! owner_.ipcManager.isConnected())
                    continue;

                std::visit ([this] (auto&& m) { dispatch (m); }, item);
            }
        }
    }

private:
    void dispatch (const MidiItem& m)        { owner_.ipcManager.sendMidi (m.msg); }
    void dispatch (const WindowPosItem& m)   { owner_.ipcManager.sendWindowPos (m.x, m.y, m.w, m.h); }
    void dispatch (const WindowTitleItem& m) { owner_.ipcManager.sendWindowTitle (m.title); }
    void dispatch (const RequestStateItem&)  { owner_.ipcManager.sendRequestState(); }
    void dispatch (const SetStateItem& m)    { owner_.ipcManager.sendSetState (m.data); }
    void dispatch (const AudioConfigItem& m) { owner_.ipcManager.sendAudioConfig (m.sampleRate, m.bufferSize); }

    BridgeInstance&         owner_;
    juce::CriticalSection   lock_;
    std::vector<Item>       queue_;
    juce::WaitableEvent     event_ { false };
};

// =====================================================================

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
    state.store (State::Connecting, std::memory_order_relaxed);

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
    ipcManager.onHeartbeat = [this] {
        lastHeartbeatMs_.store (juce::Time::currentTimeMillis(), std::memory_order_relaxed);
    };

    ipcManager.onConnected = [this] {
        state.store (State::Connected, std::memory_order_release);
        // Seed heartbeat timestamp so the watchdog doesn't fire immediately.
        lastHeartbeatMs_.store (juce::Time::currentTimeMillis(), std::memory_order_relaxed);
        juce::Logger::writeToLog ("[BridgeInstance] Connected: " + pluginPath_);
        // Start watchdog — detects silent disconnects caused by the JUCE named-pipe
        // reconnect loop (which prevents connectionLost() from firing automatically).
        heartbeatWatchdog_ = std::make_unique<HeartbeatWatchdog> (*this);
        heartbeatWatchdog_->startTimer (2000);
        if (onConnected) onConnected (this);
    };

    ipcManager.onDisconnected = [this] {
        state.store (State::Idle, std::memory_order_release);
        juce::Logger::writeToLog ("[BridgeInstance] Disconnected: " + pluginPath_);
        if (onDisconnected) onDisconnected (this);
    };

    ipcManager.onWindowPosReceived = [this] (int x, int y, int w, int h) {
        lastWindowBounds = { x, y, w, h };
    };

    ipcManager.onStateReceived = [this] (const juce::MemoryBlock& state) {
        if (onStateReceived) onStateReceived (this, state);
    };

    // Start the async IPC sender thread before spawning the child process.
    ipcSender_ = std::make_unique<IpcSenderThread> (*this);
    ipcSender_->startThread();

    // Launch the Bridge child process.
    // ChildProcess を使うことでプロセスハンドルを保持し、shutdown()時に終了待機できる。
    juce::StringArray cmdArgs;
    cmdArgs.add (bridgeExe.getFullPathName());
    cmdArgs.add ("--plugin");   cmdArgs.add (pluginPath);
    cmdArgs.add ("--ipc-pipe"); cmdArgs.add (pipeName);
    cmdArgs.add ("--shm-name"); cmdArgs.add (shmName);
    cmdArgs.add ("--sync-name"); cmdArgs.add (syncName);

    juce::Logger::writeToLog ("[BridgeInstance] Launching bridge: " + cmdArgs.joinIntoString (" "));
    if (! childProcess_.start (cmdArgs))
    {
        juce::Logger::writeToLog ("[BridgeInstance] Error: failed to start bridge process.");
        return false;
    }
    return true;
}

void BridgeInstance::HeartbeatWatchdog::timerCallback()
{
    // Guard: only act when still connected.
    if (owner_.state.load (std::memory_order_acquire) != BridgeInstance::State::Connected)
    {
        stopTimer();
        return;
    }

    auto now  = juce::Time::currentTimeMillis();
    auto last = owner_.lastHeartbeatMs_.load (std::memory_order_relaxed);

    // 5 second timeout: Bridge sends heartbeats every 1500 ms, so 3+ missed beats
    // reliably indicate a dead or disconnected Bridge process.
    if (last > 0 && (now - last) > 5000)
    {
        juce::Logger::writeToLog ("[BridgeInstance] Heartbeat timeout — forcing disconnect: "
                                  + owner_.pluginPath_);
        stopTimer();

        // IMPORTANT: JUCE's disconnect() calls safeAction->setSafe(false) after
        // stopThread(), which kills any pending ConnectionStateMessage before it can
        // deliver connectionLost(). Calling stopPipe() alone therefore does NOT
        // reliably fire connectionLost() / onDisconnected.
        //
        // Fix: manually fire onDisconnected first (same effect as connectionLost() path),
        // then call stopPipe() purely for resource cleanup (breaks reconnect loop, etc.).
        owner_.state.store (State::Idle, std::memory_order_release);
        if (owner_.onDisconnected) owner_.onDisconnected (&owner_);

        // Cleanup: signal cancelEvent → unblocks reconnect loop → ConnectionThread exits.
        // This call is now for resource teardown only; onDisconnected already fired above.
        owner_.ipcManager.stopPipe();
    }
}

void BridgeInstance::shutdown()
{
    // Stop the heartbeat watchdog before tearing down IPC so no timer fires
    // during or after pipe teardown.
    if (heartbeatWatchdog_ != nullptr)
    {
        heartbeatWatchdog_->stopTimer();
        heartbeatWatchdog_.reset();
    }

    // Signal the sender thread to exit, then immediately stop the pipe so that
    // any in-flight pipe write is unblocked by the cancelEvent signal.
    // IMPORTANT: stopPipe() must be called BEFORE stopThread() — if the thread
    // is blocked inside a pipe write (ConnectNamedPipe with INFINITE timeout),
    // signalThreadShouldExit() alone cannot wake it.  stopPipe() signals the
    // cancelEvent which breaks the wait and lets the thread observe threadShouldExit.
    if (ipcSender_ != nullptr)
        ipcSender_->signalThreadShouldExit();

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
    ipcManager.stopPipe();     // signals cancelEvent → unblocks reconnect loop and any blocked writes

    if (ipcSender_ != nullptr)
    {
        ipcSender_->stopThread (500);
        ipcSender_.reset();
    }

    syncEvents.close();
    sharedMem.close();
    state.store (State::Idle, std::memory_order_relaxed);

    // パイプ切断を受けてBridgeが自発的に終了するのを待つ。
    // メッセージスレッド上での呼び出し（Bridge Xボタン切断など）はUIをブロックしないよう
    // 即座にkillする。バックグラウンドスレッド（Coreアプリ終了時など）は最大3秒待機する。
    if (childProcess_.isRunning())
    {
        bool onMessageThread = juce::MessageManager::existsAndIsCurrentThread();
        int waitMs = onMessageThread ? 0 : 3000;
        if (! childProcess_.waitForProcessToFinish (waitMs))
        {
            juce::Logger::writeToLog ("[BridgeInstance] Killing bridge process: " + pluginPath_);
            childProcess_.kill();
        }
    }
}

std::unique_ptr<BridgeSyncProcessor> BridgeInstance::createSyncProcessor()
{
    return std::make_unique<BridgeSyncProcessor> (sharedMem, syncEvents);
}

bool BridgeInstance::sendMidi (const juce::MidiMessage& msg)
{
    if (state.load (std::memory_order_acquire) != State::Connected)
        return false;
    if (ipcSender_ == nullptr) return false;
    ipcSender_->enqueue (IpcSenderThread::MidiItem { msg });
    return true;
}

bool BridgeInstance::sendAudioConfig (float sampleRate, int32_t bufferSize)
{
    if (state.load (std::memory_order_acquire) != State::Connected)
        return false;
    if (ipcSender_ == nullptr) return false;
    ipcSender_->enqueue (IpcSenderThread::AudioConfigItem { sampleRate, bufferSize });
    return true;
}

bool BridgeInstance::sendWindowPos (int x, int y, int w, int h)
{
    if (state.load (std::memory_order_acquire) != State::Connected) return false;
    if (ipcSender_ == nullptr) return false;
    ipcSender_->enqueue (IpcSenderThread::WindowPosItem { x, y, w, h });
    return true;
}

bool BridgeInstance::sendRequestState()
{
    if (state.load (std::memory_order_acquire) != State::Connected) return false;
    if (ipcSender_ == nullptr) return false;
    ipcSender_->enqueue (IpcSenderThread::RequestStateItem {});
    return true;
}

bool BridgeInstance::sendWindowTitle (const juce::String& title)
{
    if (state.load (std::memory_order_acquire) != State::Connected) return false;
    if (ipcSender_ == nullptr) return false;
    ipcSender_->enqueue (IpcSenderThread::WindowTitleItem { title });
    return true;
}

bool BridgeInstance::sendSetState (const juce::MemoryBlock& stateBytes)
{
    if (state.load (std::memory_order_acquire) != State::Connected) return false;
    if (ipcSender_ == nullptr) return false;
    ipcSender_->enqueue (IpcSenderThread::SetStateItem { stateBytes });
    return true;
}
