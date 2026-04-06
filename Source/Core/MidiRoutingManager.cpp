#include "MidiRoutingManager.h"

MidiRoutingManager::MidiRoutingManager (const juce::OwnedArray<BridgeInstance>& bridges)
    : bridges_ (bridges)
{
}

// ── MIDI dispatch ──────────────────────────────────────────────────────────────
void MidiRoutingManager::sendMidi (const juce::MidiMessage& msg)
{
    // ── Visual feedback (Layout Studio) ───────────────────────────────────────
    if (onMidiActivity) onMidiActivity (msg);

    // ── Pad-based routing (Ch10, Note 36–51) ─────────────────────────────────
    // Ch10 pad messages are intercepted before block/legacy routing so that
    // pads always route to their assigned bridge regardless of block state.
    if (msg.getChannel() == kPadMidiChannel
        && (msg.isNoteOn() || msg.isNoteOff()))
    {
        int note = msg.getNoteNumber();
        if (note >= kPadNoteStart && note <= kPadNoteEnd)
        {
            juce::ScopedLock sl (blockRoutingLock_);
            if (msg.isNoteOn())
            {
                for (const auto& pad : padAssignments_)
                {
                    if (pad.padIndex == note - kPadNoteStart
                        && pad.targetBridge != nullptr)
                    {
                        pad.targetBridge->sendMidi (msg);
                        padActiveNotes_[note] = pad.targetBridge;
                        return;
                    }
                }
                padActiveNotes_.erase (note);  // no assignment — swallow
            }
            else  // NoteOff
            {
                auto it = padActiveNotes_.find (note);
                if (it != padActiveNotes_.end())
                {
                    it->second->sendMidi (msg);
                    padActiveNotes_.erase (it);
                }
            }
            return;  // always consume Ch10 pad-range messages
        }
    }

    // ── Block-based routing (Instrument Layout Studio) ────────────────────────
    // When blocks are defined they replace the legacy channel routing for notes.
    // Non-note messages are broadcast to all bridges so that CC, pitch-bend,
    // sustain etc. reach every instrument in the layout.
    {
        juce::ScopedLock sl (blockRoutingLock_);

        if (! keyboardBlocks_.empty())
        {
            if (msg.isNoteOn())
            {
                int rawNote = msg.getNoteNumber();
                auto& targets = activeNoteTargets_[rawNote];
                targets.clear();

                for (const auto& block : keyboardBlocks_)
                {
                    if (block.targetBridge == nullptr || ! block.containsNote (rawNote))
                        continue;

                    int shifted = block.shiftedNote (rawNote);
                    block.targetBridge->sendMidi (
                        juce::MidiMessage::noteOn (msg.getChannel(), shifted,
                                                   msg.getFloatVelocity()));
                    targets.push_back ({ block.targetBridge, shifted });
                }
                return;
            }

            if (msg.isNoteOff())
            {
                int rawNote = msg.getNoteNumber();
                auto it = activeNoteTargets_.find (rawNote);
                if (it != activeNoteTargets_.end())
                {
                    for (const auto& entry : it->second)
                        entry.bridge->sendMidi (
                            juce::MidiMessage::noteOff (msg.getChannel(), entry.shiftedNote,
                                                        msg.getFloatVelocity()));
                    activeNoteTargets_.erase (it);
                }
                return;
            }

            // Non-note messages in block mode → broadcast to all bridges.
            for (auto* b : bridges_)
                b->sendMidi (msg);
            return;
        }
    }

    // ── Legacy routing (fallback when no blocks are defined) ──────────────────
    if (routeToAll_.load (std::memory_order_relaxed))
    {
        for (auto* b : bridges_)
            b->sendMidi (msg);
    }
    else
    {
        if (auto* t = midiTargetBridge_.load (std::memory_order_relaxed))
            t->sendMidi (msg);
    }
}

// ── Routing control ────────────────────────────────────────────────────────────
void MidiRoutingManager::setRouteToAll() noexcept
{
    midiTargetBridge_.store (nullptr, std::memory_order_relaxed);
    routeToAll_.store       (true,    std::memory_order_relaxed);
}

void MidiRoutingManager::setRouteToTarget (BridgeInstance* target) noexcept
{
    midiTargetBridge_.store (target, std::memory_order_relaxed);
    routeToAll_.store       (false,  std::memory_order_relaxed);
}

bool MidiRoutingManager::isRouteToAll() const noexcept
{
    return routeToAll_.load (std::memory_order_relaxed);
}

BridgeInstance* MidiRoutingManager::getTarget() const noexcept
{
    return midiTargetBridge_.load (std::memory_order_relaxed);
}

bool MidiRoutingManager::handleBridgeDisconnected (BridgeInstance* b) noexcept
{
    bool routingChanged = false;
    if (midiTargetBridge_.load (std::memory_order_relaxed) == b)
    {
        midiTargetBridge_.store (nullptr, std::memory_order_relaxed);
        routeToAll_.store       (true,    std::memory_order_relaxed);
        routingChanged = true;
    }

    // Null out any block targets pointing to the disconnected bridge.
    {
        juce::ScopedLock sl (blockRoutingLock_);
        for (auto& block : keyboardBlocks_)
            if (block.targetBridge == b)
                block.targetBridge = nullptr;
    }

    return routingChanged;
}

// ── Octave ─────────────────────────────────────────────────────────────────────
void MidiRoutingManager::applyOctaveShift (int delta)
{
    octaveOffset_ = juce::jlimit (-3, 3, octaveOffset_ + delta);
    if (onOctaveChanged) onOctaveChanged (octaveOffset_);
}

// ── Project persistence ────────────────────────────────────────────────────────
void MidiRoutingManager::setPendingTarget (const juce::String& pluginPath)
{
    pendingMidiTarget_ = pluginPath;
}

bool MidiRoutingManager::tryApplyPendingTarget (const juce::String& pluginPath,
                                                BridgeInstance* b)
{
    if (pendingMidiTarget_.isNotEmpty() && pluginPath == pendingMidiTarget_)
    {
        midiTargetBridge_.store (b,     std::memory_order_relaxed);
        routeToAll_.store       (false, std::memory_order_relaxed);
        pendingMidiTarget_ = juce::String();
        return true;
    }
    return false;
}

void MidiRoutingManager::resetForProjectLoad()
{
    midiTargetBridge_.store (nullptr, std::memory_order_relaxed);
    routeToAll_.store       (true,    std::memory_order_relaxed);
    pendingMidiTarget_ = juce::String();
    clearBlocks();
    juce::ScopedLock sl (blockRoutingLock_);
    padAssignments_.clear();
    padActiveNotes_.clear();
}

// ── Block-based routing ────────────────────────────────────────────────────────

void MidiRoutingManager::setBlocks (std::vector<KeyboardBlock> blocks)
{
    {
        juce::ScopedLock sl (blockRoutingLock_);
        keyboardBlocks_     = std::move (blocks);
        activeNoteTargets_.clear();
    }
    resolveBlockTargets();
}

std::vector<KeyboardBlock> MidiRoutingManager::getBlocks() const
{
    juce::ScopedLock sl (blockRoutingLock_);
    return keyboardBlocks_;
}

void MidiRoutingManager::clearBlocks()
{
    juce::ScopedLock sl (blockRoutingLock_);
    keyboardBlocks_.clear();
    activeNoteTargets_.clear();
}

bool MidiRoutingManager::hasBlocks() const noexcept
{
    juce::ScopedLock sl (blockRoutingLock_);
    return ! keyboardBlocks_.empty();
}

void MidiRoutingManager::resolveBlockTargets() noexcept
{
    juce::ScopedLock sl (blockRoutingLock_);
    for (auto& block : keyboardBlocks_)
    {
        block.targetBridge = nullptr;
        for (auto* b : bridges_)
            if (b->getPluginPath() == block.targetPluginPath
                    && b->getState() == BridgeInstance::State::Connected)
            {
                block.targetBridge = b;
                break;
            }
    }
}

void MidiRoutingManager::notifyBridgeConnected (BridgeInstance* b) noexcept
{
    juce::ScopedLock sl (blockRoutingLock_);
    for (auto& block : keyboardBlocks_)
        if (block.targetBridge == nullptr
                && block.targetPluginPath == b->getPluginPath())
            block.targetBridge = b;
    for (auto& pad : padAssignments_)
        if (pad.targetBridge == nullptr
                && pad.targetPluginPath == b->getPluginPath())
            pad.targetBridge = b;
}

// ── Pad-based routing ─────────────────────────────────────────────────────────

void MidiRoutingManager::setPadAssignments (std::vector<PadAssignment> pads)
{
    {
        juce::ScopedLock sl (blockRoutingLock_);
        padAssignments_ = std::move (pads);
        padActiveNotes_.clear();
    }
    resolvePadTargets();
}

std::vector<PadAssignment> MidiRoutingManager::getPadAssignments() const
{
    juce::ScopedLock sl (blockRoutingLock_);
    return padAssignments_;
}

void MidiRoutingManager::resolvePadTargets() noexcept
{
    juce::ScopedLock sl (blockRoutingLock_);
    for (auto& pad : padAssignments_)
    {
        pad.targetBridge = nullptr;
        for (auto* b : bridges_)
            if (b->getPluginPath() == pad.targetPluginPath
                    && b->getState() == BridgeInstance::State::Connected)
            {
                pad.targetBridge = b;
                break;
            }
    }
}
