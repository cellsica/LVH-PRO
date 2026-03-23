#include "MidiRoutingManager.h"

MidiRoutingManager::MidiRoutingManager (const juce::OwnedArray<BridgeInstance>& bridges)
    : bridges_ (bridges)
{
}

// ── MIDI dispatch ──────────────────────────────────────────────────────────────
void MidiRoutingManager::sendMidi (const juce::MidiMessage& msg)
{
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
    if (midiTargetBridge_.load (std::memory_order_relaxed) == b)
    {
        midiTargetBridge_.store (nullptr, std::memory_order_relaxed);
        routeToAll_.store       (true,    std::memory_order_relaxed);
        return true;
    }
    return false;
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
}
