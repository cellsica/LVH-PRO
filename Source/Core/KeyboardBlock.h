#pragma once
#include <JuceHeader.h>

class BridgeInstance;  // Forward declaration — full type not needed here.

/**
 * @file KeyboardBlock.h
 * @brief Defines KeyboardBlock — the unit of keyboard-split / layer assignment
 *        for Instrument Layout Studio.
 *
 * @note Mission 054 Phase A
 */

/**
 * @struct KeyboardBlock
 * @brief Represents a contiguous MIDI note range assigned to a single Bridge (Mixer CH).
 *
 * Blocks are owned by MidiRoutingManager and drive the block-based MIDI routing
 * path.  When one or more blocks are defined, MidiRoutingManager dispatches
 * NoteOn/Off messages according to the block layout instead of the legacy
 * channel-based routing.
 *
 * **Overlap / Layer:**
 * Multiple blocks may share the same note range.  Every block whose range
 * contains the incoming note receives the message — this is the "layer" feature.
 *
 * **Serialisation:**
 * @p targetPluginPath and the note-range / octaveShift fields are persisted to
 * the @c <LayoutStudio> section of the @c .lvh project file.
 * @p targetBridge is a runtime pointer resolved by
 * MidiRoutingManager::resolveBlockTargets().
 *
 * **Note range snap:**
 * startNote / endNote are arbitrary MIDI note numbers [0, 127].
 * The Phase C UI will snap to C-based octave boundaries (multiples of 12).
 */
struct KeyboardBlock
{
    int          startNote       = 48;         ///< First MIDI note number in range (inclusive).
    int          endNote         = 59;         ///< Last  MIDI note number in range (inclusive).
    int          octaveShift     = 0;          ///< Per-block transpose: ±3 octaves.
    juce::String targetPluginPath;             ///< Key used for persistence and target resolution.
    juce::Colour blockColour     { 0xff556688u }; ///< Display colour; synced with Mixer CH in Phase C.

    // ── Runtime only ──────────────────────────────────────────────────────────
    /** Resolved from @p targetPluginPath by MidiRoutingManager::resolveBlockTargets().
     *  Null when the bridge is not currently connected.  Not serialised. */
    BridgeInstance* targetBridge = nullptr;

    // ── Helpers ───────────────────────────────────────────────────────────────

    /** @return true if @p note falls within [startNote, endNote]. */
    bool containsNote (int note) const noexcept
    {
        return note >= startNote && note <= endNote;
    }

    /**
     * @brief Apply this block's octaveShift to a raw MIDI note number.
     * @param rawNote  Incoming MIDI note number [0, 127].
     * @return Clamped, shifted note number [0, 127].
     */
    int shiftedNote (int rawNote) const noexcept
    {
        return juce::jlimit (0, 127, rawNote + octaveShift * 12);
    }
};
