#pragma once

/**
 * @file ThemePalette.h
 * @brief Application-wide colour palette loaded from an external JSON theme file.
 *
 * All UI components use ThemePalette::get(ColourId::Xxx) instead of hardcoded
 * Colour(0xff...) literals, making the entire skin swappable at startup.
 *
 * **Usage:**
 * @code
 * g.setColour(ThemePalette::get(ColourId::BgPrimary));
 * @endcode
 *
 * **Startup sequence:**
 * 1. LvhProApplication calls ThemePalette::load() with the chosen JSON file.
 * 2. If the file is missing or a key is absent, built-in Dark defaults are used.
 * 3. Theme changes take effect after restarting the application.
 */

#include <JuceHeader.h>

// =============================================================================
// ColourId — semantic colour identifiers
// =============================================================================

enum class ColourId : size_t
{
    // ── Backgrounds ───────────────────────────────────────────────────────────
    BgPrimary = 0,      ///< Main application background.
    BgPanel,            ///< Panel / container background.
    BgPanelAlt,         ///< Secondary panel, separators.
    BgInput,            ///< Text input / form fields.
    BgLog,              ///< MIDI / system log panels.
    BgVisualizer,       ///< Visualizer / full-black backgrounds.

    // ── Generic controls ──────────────────────────────────────────────────────
    CtrlNormal,         ///< Button default (inactive) background.
    CtrlActive,         ///< Button / filter active state.
    CtrlHover,          ///< Hover background.

    // ── Mixer strip special backgrounds ───────────────────────────────────────
    MixerInputBg,       ///< INPUT strip background.
    MixerMasterBg,      ///< MASTER strip background.
    MixerBypassBg,      ///< Strip background when plugin is bypassed.
    MixerFxSlotBg,      ///< FX slot placeholder background.
    MixerFxArea,        ///< FX slot area background.

    // ── Accent colours (feature-toggle ON state) ──────────────────────────────
    AccentGreen,        ///< Keyboard / Stage Play ON.
    AccentBlue,         ///< Monitor / MIDI ON.
    AccentOrange,       ///< Mixer window toggle ON.
    AccentPurple,       ///< Stage window toggle ON.
    AccentViolet,       ///< VU Meter toggle ON.
    AccentDarkGreen,    ///< Metronome toggle ON.
    AccentDarkBlue,     ///< Visualizer toggle ON.

    // ── State colours ─────────────────────────────────────────────────────────
    StateSelected,      ///< Selected item highlight (blue).
    StateSelectedText,  ///< Text on selected items (light blue).
    StateActive,        ///< Active / playing state (green).
    StateBypass,        ///< Bypass active (red).
    StateMuted,         ///< Muted background (dark red).
    StatePin,           ///< Pin button active (brown).
    StateQueue,         ///< Global cue button active (dark gold).
    StateMono,          ///< Mono button active (dark blue).

    // ── Text ──────────────────────────────────────────────────────────────────
    TextPrimary,        ///< Main text (white).
    TextSecondary,      ///< Label text (light purple-grey).
    TextTertiary,       ///< Settings labels (bright grey).
    TextMidi,           ///< MIDI monitor text (light blue).
    TextSysLog,         ///< System log text (green).
    TextInactive,       ///< Inactive / dim text.
    TextSustainOn,      ///< Sustain ON indicator (bright green).
    TextFxOverlay,      ///< FX slot overlay text (blue-white).
    TextPan,            ///< Pan fader label (grey).
    TextStagePath,      ///< Stage item file path (grey).
    TextSettingsHeader, ///< Settings section header (orange).

    // ── Meters ────────────────────────────────────────────────────────────────
    MeterGreen,         ///< Level meter safe zone.
    MeterYellow,        ///< Level meter warning zone.
    MeterRed,           ///< Level meter clip zone.

    // ── Borders ───────────────────────────────────────────────────────────────
    BorderDefault,      ///< Default divider / resizer.
    BorderOutline,      ///< List / panel outline.
    BorderActive,       ///< Selected item border.

    // ── Mixer strip user palette ───────────────────────────────────────────────
    PaletteBlue,        ///< Steel Blue strip accent.
    PaletteRed,         ///< Dark Red strip accent.
    PaletteGreen,       ///< Forest Green strip accent.
    PaletteOrange,      ///< Burnt Orange strip accent.
    PaletteViolet,      ///< Violet strip accent.
    PaletteTeal,        ///< Teal strip accent.

    // ── Stage window ──────────────────────────────────────────────────────────
    StageBg,            ///< Stage window background.
    StageItemNormal,    ///< Stage list item background.
    StageItemSelected,  ///< Stage list item selected background.
    StageItemActive,    ///< Stage list item active/loaded background.
    StageCtrlNormal,    ///< Stage control button normal.
    StageCtrlActive,    ///< Stage control button active.

    // ── Plugin picker / list boxes ─────────────────────────────────────────────
    ListBg,             ///< List box background.
    ListOutline,        ///< List box outline.

    // ── Settings window ────────────────────────────────────────────────────────
    SettingsSelected,   ///< Settings list selected row background.
    SettingsRescan,     ///< Rescan button background.
    SettingsPathText,   ///< Path list selected row text.
    SettingsSepLine,    ///< Path list separator line.

    // ── Metronome window ───────────────────────────────────────────────────────
    MetroBeatOff,       ///< Beat indicator inactive background.
    MetroBeatBg,        ///< Beat indicator area background.
    MetroTap,           ///< Tap Tempo button background.
    MetroBpm,           ///< BPM adjust button background.

    numColours          ///< Sentinel — do not use directly.
};

// =============================================================================
// ThemePalette
// =============================================================================

/**
 * @class ThemePalette
 * @brief Singleton that holds the active colour palette.
 *
 * Call load() once at startup.  All subsequent calls to get() return
 * the loaded colours.  Missing JSON keys fall back to built-in Dark defaults.
 */
class ThemePalette
{
public:
    /** @brief Access the singleton instance. */
    static ThemePalette& getInstance();

    /**
     * @brief Load palette from a JSON theme file.
     *
     * Keys in the JSON must match the ColourId enum names exactly (e.g. "BgPrimary").
     * Values are 6-character RRGGBB hex strings (e.g. "14141f").
     * Missing keys keep their built-in default values.
     *
     * @param jsonFile  Path to the JSON theme file.
     */
    void load (const juce::File& jsonFile);

    /**
     * @brief Reset to built-in defaults without loading a file.
     * @param light  true = Light theme defaults, false = Dark theme defaults.
     */
    void resetToDefaults (bool light = false);

    /**
     * @brief Return the colour for the given semantic ID.
     * @param id  A ColourId enumerator.
     */
    static juce::Colour get (ColourId id) noexcept;

private:
    ThemePalette();

    /** Set a single colour by its string key (called during JSON parsing). */
    void setByKey (const juce::String& key, juce::uint32 argb);

    std::array<juce::Colour, static_cast<size_t> (ColourId::numColours)> colours_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemePalette)
};
