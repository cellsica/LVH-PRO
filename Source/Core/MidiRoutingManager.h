#pragma once
#include <JuceHeader.h>
#include "BridgeInstance.h"

/**
 * @class MidiRoutingManager
 * @brief Manages MIDI routing, octave transposition, and project-restore targeting.
 *
 * Owns all MIDI routing state extracted from LvhProApplication:
 * - Route-to-all vs. single-target selection (atomic, MIDI-thread safe).
 * - Octave offset applied to all outgoing note messages (message thread only).
 * - Pending MIDI target path used to restore routing on project load.
 *
 * **Thread safety:**
 * - sendMidi() is safe to call from the MIDI input thread or the message thread.
 * - All other public methods must be called from the message thread.
 *
 * **Lifetime:** LvhProApplication holds this as a member declared *after* the
 * bridge array, so the const-ref stored internally is always valid for the
 * application lifetime.
 */
class MidiRoutingManager
{
public:
    /**
     * @brief Constructs a MidiRoutingManager.
     * @param bridges  The application-owned bridge array.  Must outlive this object.
     */
    explicit MidiRoutingManager (const juce::OwnedArray<BridgeInstance>& bridges);

    /**
     * @brief Fired on the message thread when the octave offset changes.
     *
     * The caller (LvhProApplication) is responsible for updating the PC-keyboard
     * listener and the MainComponent UI accordingly.
     *
     * @param newOffset  The new raw octave offset value in the range [-3, +3].
     */
    std::function<void(int newOffset)> onOctaveChanged;

    // ── MIDI dispatch ─────────────────────────────────────────────────────────

    /**
     * @brief Dispatch a MIDI message to the current routing target.
     *
     * Applies the current octave shift to note-on/off messages, then forwards
     * to the target bridge (or all bridges in route-to-all mode).
     * Safe to call from the MIDI input thread or the message thread.
     *
     * @param msg  The MIDI message to send.
     */
    void sendMidi (const juce::MidiMessage& msg);

    // ── Routing control ───────────────────────────────────────────────────────

    /** @brief Switch to broadcast mode — all connected bridges receive MIDI. */
    void setRouteToAll() noexcept;

    /**
     * @brief Route MIDI exclusively to @p target.
     * @param target  The bridge that should receive all MIDI input.  Must not be nullptr.
     */
    void setRouteToTarget (BridgeInstance* target) noexcept;

    /** @brief Returns true if currently routing to all bridges. */
    bool isRouteToAll() const noexcept;

    /**
     * @brief Returns the current single-target bridge, or nullptr in route-to-all mode.
     * @return Pointer to the target BridgeInstance, or nullptr.
     */
    BridgeInstance* getTarget() const noexcept;

    /**
     * @brief Handle a bridge disconnection.
     *
     * If @p b was the current MIDI target, automatically falls back to
     * route-to-all mode.
     *
     * @param b  The bridge that disconnected.
     * @return   true if routing was changed (caller should update the UI).
     */
    bool handleBridgeDisconnected (BridgeInstance* b) noexcept;

    // ── Octave shift ──────────────────────────────────────────────────────────

    /**
     * @brief Apply a relative octave shift.
     *
     * Clamps the result to [-3, +3] and fires onOctaveChanged.
     *
     * @param delta  Number of octaves to shift (+1 or -1 typically).
     */
    void applyOctaveShift (int delta);

    /**
     * @brief Returns the current octave offset.
     * @return Integer offset in the range [-3, +3].
     */
    int getOctaveOffset() const noexcept { return octaveOffset_; }

    // ── Project persistence ───────────────────────────────────────────────────

    /**
     * @brief Store the plugin path of the MIDI target to be restored after a project load.
     *
     * The path is matched against connecting bridges via tryApplyPendingTarget().
     *
     * @param pluginPath  Absolute path to the VST3/CLAP plugin file.
     */
    void setPendingTarget (const juce::String& pluginPath);

    /**
     * @brief Attempt to restore the pending MIDI target routing.
     *
     * If @p pluginPath matches the stored pending target, applies routing to
     * @p b and clears the pending state.
     *
     * @param pluginPath  Path of the bridge that just connected.
     * @param b           The newly-connected BridgeInstance.
     * @return            true if routing was applied and the pending target was consumed.
     */
    bool tryApplyPendingTarget (const juce::String& pluginPath, BridgeInstance* b);

    /**
     * @brief Reset routing state for a new project load.
     *
     * Sets mode to route-to-all and clears any pending target path.
     * Call this at the start of ProjectSerializer::loadProject().
     */
    void resetForProjectLoad();

private:
    const juce::OwnedArray<BridgeInstance>& bridges_;

    /// Accessed from MIDI input thread and message thread — atomic.
    std::atomic<bool>            routeToAll_       { true };
    /// Accessed from MIDI input thread and message thread — atomic.
    std::atomic<BridgeInstance*> midiTargetBridge_ { nullptr };

    int          octaveOffset_     = 0;  ///< Message thread only.
    juce::String pendingMidiTarget_;     ///< Message thread only.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRoutingManager)
};
