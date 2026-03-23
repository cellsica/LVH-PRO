#pragma once
#include <JuceHeader.h>
#include "BridgeInstance.h"

// =====================================================================
// MidiRoutingManager
//
// Owns all MIDI routing state extracted from LvhProApplication:
//   - routeToAll / midiTargetBridge  (atomic, MIDI-thread safe)
//   - octaveOffset                   (message thread only)
//   - pendingMidiTarget              (message thread only; set on project load,
//                                     consumed in onConnected)
//
// LvhProApplication holds this as a member (declared after `bridges` so the
// reference stored here is always valid for the application lifetime).
// =====================================================================
class MidiRoutingManager
{
public:
    // bridges reference must outlive this object (both are members of LvhProApplication).
    explicit MidiRoutingManager (const juce::OwnedArray<BridgeInstance>& bridges);

    // ── Callback — wired by LvhProApplication in wireUICallbacks() ───────
    // Fired after octaveOffset changes; newOffset is the raw offset value (-3…+3).
    // Caller is responsible for updating pcKeyListener and MainComponent UI.
    std::function<void(int newOffset)> onOctaveChanged;

    // ── MIDI dispatch — MIDI thread + message thread safe ─────────────────
    void sendMidi (const juce::MidiMessage& msg);

    // ── Routing control — message thread only ─────────────────────────────
    void            setRouteToAll()                    noexcept;
    void            setRouteToTarget (BridgeInstance*) noexcept;
    bool            isRouteToAll()      const noexcept;
    BridgeInstance* getTarget()         const noexcept;

    // Called when a bridge disconnects.  If it was the MIDI target, falls back
    // to All mode and returns true (caller should notify the UI).
    bool handleBridgeDisconnected (BridgeInstance* b) noexcept;

    // ── Octave — message thread only ──────────────────────────────────────
    void applyOctaveShift (int delta);
    int  getOctaveOffset() const noexcept { return octaveOffset_; }

    // ── Project persistence helpers — message thread only ─────────────────
    // Store the plugin path of the MIDI target to be restored on connect.
    void setPendingTarget (const juce::String& pluginPath);

    // If pluginPath matches the pending target, applies routing and clears it.
    // Returns true on a successful match.
    bool tryApplyPendingTarget (const juce::String& pluginPath, BridgeInstance* b);

    // Reset routing state to "All" and clear any pending target (call on project load).
    void resetForProjectLoad();

private:
    const juce::OwnedArray<BridgeInstance>& bridges_;

    // Accessed from MIDI input thread and message thread — use atomics.
    std::atomic<bool>            routeToAll_       { true };
    std::atomic<BridgeInstance*> midiTargetBridge_ { nullptr };

    int          octaveOffset_     = 0;  // message thread only
    juce::String pendingMidiTarget_;     // message thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRoutingManager)
};
