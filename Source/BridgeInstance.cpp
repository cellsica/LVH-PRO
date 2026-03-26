#include "BridgeInstance.h"

// =====================================================================
// MidiSenderThread
//
// Sends MIDI messages to the Bridge process on a dedicated thread so
// that named-pipe writes never block the JUCE message thread.
// The message thread calls enqueue() (non-blocking); this thread does
// the actual ipcManager.sendMidi() call which may stall briefly.
// =====================================================================
class BridgeInstance::MidiSenderThread : public juce::Thread
{
public:
    MidiSenderThread (BridgeInstance& owner)
        : juce::Thread ("BridgeMidiSender"), owner_ (owner) {}

    // Thread-safe, non-blocking enqueue from the message thread.
    void enqueue (const juce::MidiMessage& msg)
    {
        {
            juce::ScopedLock sl (lock_);
            queue_.push_back (msg);
        }
        event_.signal();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            event_.wait (100);

            // Swap under lock to minimise lock-hold time during pipe writes.
            std::vector<juce::MidiMessage> toSend;
            {
                juce::ScopedLock sl (lock_);
                toSend.swap (queue_);
            }

            for (auto& m : toSend)
            {
                if (owner_.state.load (std::memory_order_acquire) == BridgeInstance::State::Connected
                    && owner_.ipcManager.isConnected())
                    owner_.ipcManager.sendMidi (m);
            }
        }
    }

private:
    BridgeInstance&                  owner_;
    juce::CriticalSection            lock_;
    std::vector<juce::MidiMessage>   queue_;
    juce::WaitableEvent              event_ { false };
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

    // Start the async MIDI sender thread before spawning the child process.
    midiSender_ = std::make_unique<MidiSenderThread> (*this);
    midiSender_->startThread();

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

    // Stop the MIDI sender thread first so no new pipe writes are attempted
    // after the IPC manager is torn down.
    if (midiSender_ != nullptr)
    {
        midiSender_->signalThreadShouldExit();
        midiSender_->stopThread (500);
        midiSender_.reset();
    }

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
    // Enqueue for async delivery — the MidiSenderThread does the actual pipe
    // write so the JUCE message thread is never blocked by a slow Bridge read.
    if (midiSender_ != nullptr)
    {
        midiSender_->enqueue (msg);
        return true;
    }
    // Fallback (thread not started yet): direct send.
    if (! ipcManager.isConnected())
        return false;
    return ipcManager.sendMidi (msg);
}

bool BridgeInstance::sendAudioConfig (float sampleRate, int32_t bufferSize)
{
    if (state.load (std::memory_order_acquire) != State::Connected)
        return false;
    return ipcManager.sendAudioConfig (sampleRate, bufferSize);
}

bool BridgeInstance::sendWindowPos (int x, int y, int w, int h)
{
    if (state.load (std::memory_order_acquire) != State::Connected) return false;
    return ipcManager.sendWindowPos (x, y, w, h);
}

bool BridgeInstance::sendRequestState()
{
    if (state.load (std::memory_order_acquire) != State::Connected) return false;
    return ipcManager.sendRequestState();
}

bool BridgeInstance::sendWindowTitle (const juce::String& title)
{
    if (state.load (std::memory_order_acquire) != State::Connected) return false;
    return ipcManager.sendWindowTitle (title);
}

bool BridgeInstance::sendSetState (const juce::MemoryBlock& stateBytes)
{
    if (state.load (std::memory_order_acquire) != State::Connected) return false;
    return ipcManager.sendSetState (stateBytes);
}
