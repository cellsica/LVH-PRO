#include "ThemePalette.h"

// =============================================================================
// Key → ColourId mapping table
// =============================================================================

static const std::pair<const char*, ColourId> kKeyMap[] =
{
    { "BgPrimary",           ColourId::BgPrimary },
    { "BgPanel",             ColourId::BgPanel },
    { "BgPanelAlt",          ColourId::BgPanelAlt },
    { "BgInput",             ColourId::BgInput },
    { "BgLog",               ColourId::BgLog },
    { "BgVisualizer",        ColourId::BgVisualizer },
    { "CtrlNormal",          ColourId::CtrlNormal },
    { "CtrlActive",          ColourId::CtrlActive },
    { "CtrlHover",           ColourId::CtrlHover },
    { "MixerInputBg",        ColourId::MixerInputBg },
    { "MixerMasterBg",       ColourId::MixerMasterBg },
    { "MixerBypassBg",       ColourId::MixerBypassBg },
    { "MixerFxSlotBg",       ColourId::MixerFxSlotBg },
    { "MixerFxArea",         ColourId::MixerFxArea },
    { "AccentGreen",         ColourId::AccentGreen },
    { "AccentBlue",          ColourId::AccentBlue },
    { "AccentOrange",        ColourId::AccentOrange },
    { "AccentPurple",        ColourId::AccentPurple },
    { "AccentViolet",        ColourId::AccentViolet },
    { "AccentDarkGreen",     ColourId::AccentDarkGreen },
    { "AccentDarkBlue",      ColourId::AccentDarkBlue },
    { "StateSelected",       ColourId::StateSelected },
    { "StateSelectedText",   ColourId::StateSelectedText },
    { "StateActive",         ColourId::StateActive },
    { "StateBypass",         ColourId::StateBypass },
    { "StateMuted",          ColourId::StateMuted },
    { "StatePin",            ColourId::StatePin },
    { "StateQueue",          ColourId::StateQueue },
    { "StateMono",           ColourId::StateMono },
    { "TextPrimary",         ColourId::TextPrimary },
    { "TextSecondary",       ColourId::TextSecondary },
    { "TextTertiary",        ColourId::TextTertiary },
    { "TextMidi",            ColourId::TextMidi },
    { "TextSysLog",          ColourId::TextSysLog },
    { "TextInactive",        ColourId::TextInactive },
    { "TextSustainOn",       ColourId::TextSustainOn },
    { "TextFxOverlay",       ColourId::TextFxOverlay },
    { "TextPan",             ColourId::TextPan },
    { "TextStagePath",       ColourId::TextStagePath },
    { "TextSettingsHeader",  ColourId::TextSettingsHeader },
    { "MeterGreen",          ColourId::MeterGreen },
    { "MeterYellow",         ColourId::MeterYellow },
    { "MeterRed",            ColourId::MeterRed },
    { "BorderDefault",       ColourId::BorderDefault },
    { "BorderOutline",       ColourId::BorderOutline },
    { "BorderActive",        ColourId::BorderActive },
    { "PaletteBlue",         ColourId::PaletteBlue },
    { "PaletteRed",          ColourId::PaletteRed },
    { "PaletteGreen",        ColourId::PaletteGreen },
    { "PaletteOrange",       ColourId::PaletteOrange },
    { "PaletteViolet",       ColourId::PaletteViolet },
    { "PaletteTeal",         ColourId::PaletteTeal },
    { "StageBg",             ColourId::StageBg },
    { "StageItemNormal",     ColourId::StageItemNormal },
    { "StageItemSelected",   ColourId::StageItemSelected },
    { "StageItemActive",     ColourId::StageItemActive },
    { "StageCtrlNormal",     ColourId::StageCtrlNormal },
    { "StageCtrlActive",     ColourId::StageCtrlActive },
    { "ListBg",              ColourId::ListBg },
    { "ListOutline",         ColourId::ListOutline },
    { "SettingsSelected",    ColourId::SettingsSelected },
    { "SettingsRescan",      ColourId::SettingsRescan },
    { "SettingsPathText",    ColourId::SettingsPathText },
    { "SettingsSepLine",     ColourId::SettingsSepLine },
    { "MetroBeatOff",        ColourId::MetroBeatOff },
    { "MetroBeatBg",         ColourId::MetroBeatBg },
    { "MetroTap",            ColourId::MetroTap },
    { "MetroBpm",            ColourId::MetroBpm },
};

// =============================================================================
// Dark theme defaults  (built-in fallback — matches themes/dark.json)
// =============================================================================

static void applyDarkDefaults (std::array<juce::Colour, static_cast<size_t>(ColourId::numColours)>& c)
{
    using C = juce::Colour;
    c[static_cast<size_t>(ColourId::BgPrimary)]          = C (0xff14141f);
    c[static_cast<size_t>(ColourId::BgPanel)]             = C (0xff1a1a2a);
    c[static_cast<size_t>(ColourId::BgPanelAlt)]          = C (0xff1e1e30);
    c[static_cast<size_t>(ColourId::BgInput)]             = C (0xff2a2a3a);
    c[static_cast<size_t>(ColourId::BgLog)]               = C (0xff0e0e18);
    c[static_cast<size_t>(ColourId::BgVisualizer)]        = C (0xff000000);

    c[static_cast<size_t>(ColourId::CtrlNormal)]          = C (0xff2a2a3a);
    c[static_cast<size_t>(ColourId::CtrlActive)]          = C (0xff3a3a5a);
    c[static_cast<size_t>(ColourId::CtrlHover)]           = C (0xff303050);

    c[static_cast<size_t>(ColourId::MixerInputBg)]        = C (0xff1a4a3a);
    c[static_cast<size_t>(ColourId::MixerMasterBg)]       = C (0xff444455);
    c[static_cast<size_t>(ColourId::MixerBypassBg)]       = C (0xff1e1414);
    c[static_cast<size_t>(ColourId::MixerFxSlotBg)]       = C (0xff2a2a38);
    c[static_cast<size_t>(ColourId::MixerFxArea)]         = C (0xff1e1e30);

    c[static_cast<size_t>(ColourId::AccentGreen)]         = C (0xff2a5a2a);
    c[static_cast<size_t>(ColourId::AccentBlue)]          = C (0xff1a3a5a);
    c[static_cast<size_t>(ColourId::AccentOrange)]        = C (0xff5a3a1a);
    c[static_cast<size_t>(ColourId::AccentPurple)]        = C (0xff3a2a5a);
    c[static_cast<size_t>(ColourId::AccentViolet)]        = C (0xff4a2a5a);
    c[static_cast<size_t>(ColourId::AccentDarkGreen)]     = C (0xff1a3a1a);
    c[static_cast<size_t>(ColourId::AccentDarkBlue)]      = C (0xff1a1a3a);

    c[static_cast<size_t>(ColourId::StateSelected)]       = C (0xff1a3a5a);
    c[static_cast<size_t>(ColourId::StateSelectedText)]   = C (0xffaaccff);
    c[static_cast<size_t>(ColourId::StateActive)]         = C (0xff22cc44);
    c[static_cast<size_t>(ColourId::StateBypass)]         = C (0xffcc3333);
    c[static_cast<size_t>(ColourId::StateMuted)]          = C (0xff881111);
    c[static_cast<size_t>(ColourId::StatePin)]            = C (0xffaa6600);
    c[static_cast<size_t>(ColourId::StateQueue)]          = C (0xffb8860b);
    c[static_cast<size_t>(ColourId::StateMono)]           = C (0xff226699);

    c[static_cast<size_t>(ColourId::TextPrimary)]         = C (0xffffffff);
    c[static_cast<size_t>(ColourId::TextSecondary)]       = C (0xffaaaacc);
    c[static_cast<size_t>(ColourId::TextTertiary)]        = C (0xffbbbbbb);
    c[static_cast<size_t>(ColourId::TextMidi)]            = C (0xff88aaff);
    c[static_cast<size_t>(ColourId::TextSysLog)]          = C (0xff88cc88);
    c[static_cast<size_t>(ColourId::TextInactive)]        = C (0xff444455);
    c[static_cast<size_t>(ColourId::TextSustainOn)]       = C (0xff00ff88);
    c[static_cast<size_t>(ColourId::TextFxOverlay)]       = C (0xffaaccff);
    c[static_cast<size_t>(ColourId::TextPan)]             = C (0xff888899);
    c[static_cast<size_t>(ColourId::TextStagePath)]       = C (0xff888899);
    c[static_cast<size_t>(ColourId::TextSettingsHeader)]  = C (0xffffaa44);

    c[static_cast<size_t>(ColourId::MeterGreen)]          = C (0xff00dd44);
    c[static_cast<size_t>(ColourId::MeterYellow)]         = C (0xffffcc00);
    c[static_cast<size_t>(ColourId::MeterRed)]            = C (0xffff2222);

    c[static_cast<size_t>(ColourId::BorderDefault)]       = C (0xff333344);
    c[static_cast<size_t>(ColourId::BorderOutline)]       = C (0xff333344);
    c[static_cast<size_t>(ColourId::BorderActive)]        = C (0xff4488cc);

    c[static_cast<size_t>(ColourId::PaletteBlue)]         = C (0xff2a4a6a);
    c[static_cast<size_t>(ColourId::PaletteRed)]          = C (0xff6a2a2a);
    c[static_cast<size_t>(ColourId::PaletteGreen)]        = C (0xff2a6a2a);
    c[static_cast<size_t>(ColourId::PaletteOrange)]       = C (0xff6a4a2a);
    c[static_cast<size_t>(ColourId::PaletteViolet)]       = C (0xff4a2a6a);
    c[static_cast<size_t>(ColourId::PaletteTeal)]         = C (0xff2a6a6a);

    c[static_cast<size_t>(ColourId::StageBg)]             = C (0xff12121c);
    c[static_cast<size_t>(ColourId::StageItemNormal)]     = C (0xff1a1a24);
    c[static_cast<size_t>(ColourId::StageItemSelected)]   = C (0xff1e1e30);
    c[static_cast<size_t>(ColourId::StageItemActive)]     = C (0xff0d3d1a);
    c[static_cast<size_t>(ColourId::StageCtrlNormal)]     = C (0xff1a3a1a);
    c[static_cast<size_t>(ColourId::StageCtrlActive)]     = C (0xff22cc44);

    c[static_cast<size_t>(ColourId::ListBg)]              = C (0xff1a1a2a);
    c[static_cast<size_t>(ColourId::ListOutline)]         = C (0xff3a3a4a);

    c[static_cast<size_t>(ColourId::SettingsSelected)]    = C (0xff1a3a5a);
    c[static_cast<size_t>(ColourId::SettingsRescan)]      = C (0xff1a3a1a);
    c[static_cast<size_t>(ColourId::SettingsPathText)]    = C (0xffaaccff);
    c[static_cast<size_t>(ColourId::SettingsSepLine)]     = C (0xff333355);

    c[static_cast<size_t>(ColourId::MetroBeatOff)]        = C (0xff1c1c2c);
    c[static_cast<size_t>(ColourId::MetroBeatBg)]         = C (0xff2a2a40);
    c[static_cast<size_t>(ColourId::MetroTap)]            = C (0xff2a3a55);
    c[static_cast<size_t>(ColourId::MetroBpm)]            = C (0xff2a2a3e);
}

// =============================================================================
// Light theme defaults  (built-in fallback — matches themes/light.json)
// =============================================================================

static void applyLightDefaults (std::array<juce::Colour, static_cast<size_t>(ColourId::numColours)>& c)
{
    using C = juce::Colour;
    c[static_cast<size_t>(ColourId::BgPrimary)]          = C (0xffe0e0e8);
    c[static_cast<size_t>(ColourId::BgPanel)]             = C (0xfff0f0f8);
    c[static_cast<size_t>(ColourId::BgPanelAlt)]          = C (0xffe8e8f0);
    c[static_cast<size_t>(ColourId::BgInput)]             = C (0xfffafafa);
    c[static_cast<size_t>(ColourId::BgLog)]               = C (0xfff5f5ff);
    c[static_cast<size_t>(ColourId::BgVisualizer)]        = C (0xff000000);

    c[static_cast<size_t>(ColourId::CtrlNormal)]          = C (0xffd8d8e8);
    c[static_cast<size_t>(ColourId::CtrlActive)]          = C (0xffa0b8d8);
    c[static_cast<size_t>(ColourId::CtrlHover)]           = C (0xffc8d0e0);

    c[static_cast<size_t>(ColourId::MixerInputBg)]        = C (0xffe8f0e8);
    c[static_cast<size_t>(ColourId::MixerMasterBg)]       = C (0xffe8e8f8);
    c[static_cast<size_t>(ColourId::MixerBypassBg)]       = C (0xfff0e8e8);
    c[static_cast<size_t>(ColourId::MixerFxSlotBg)]       = C (0xfff0f0f8);
    c[static_cast<size_t>(ColourId::MixerFxArea)]         = C (0xffe8e8f4);

    c[static_cast<size_t>(ColourId::AccentGreen)]         = C (0xff7acc7a);
    c[static_cast<size_t>(ColourId::AccentBlue)]          = C (0xff6699cc);
    c[static_cast<size_t>(ColourId::AccentOrange)]        = C (0xffcc8833);
    c[static_cast<size_t>(ColourId::AccentPurple)]        = C (0xff9977cc);
    c[static_cast<size_t>(ColourId::AccentViolet)]        = C (0xffaa66cc);
    c[static_cast<size_t>(ColourId::AccentDarkGreen)]     = C (0xff44aa44);
    c[static_cast<size_t>(ColourId::AccentDarkBlue)]      = C (0xff4466cc);

    c[static_cast<size_t>(ColourId::StateSelected)]       = C (0xff6699cc);
    c[static_cast<size_t>(ColourId::StateSelectedText)]   = C (0xff003366);
    c[static_cast<size_t>(ColourId::StateActive)]         = C (0xff44aa44);
    c[static_cast<size_t>(ColourId::StateBypass)]         = C (0xffcc4444);
    c[static_cast<size_t>(ColourId::StateMuted)]          = C (0xffddaaaa);
    c[static_cast<size_t>(ColourId::StatePin)]            = C (0xffaa8833);
    c[static_cast<size_t>(ColourId::StateQueue)]          = C (0xffaaaa44);
    c[static_cast<size_t>(ColourId::StateMono)]           = C (0xff4488bb);

    c[static_cast<size_t>(ColourId::TextPrimary)]         = C (0xff111122);
    c[static_cast<size_t>(ColourId::TextSecondary)]       = C (0xff445566);
    c[static_cast<size_t>(ColourId::TextTertiary)]        = C (0xff333344);
    c[static_cast<size_t>(ColourId::TextMidi)]            = C (0xff224499);
    c[static_cast<size_t>(ColourId::TextSysLog)]          = C (0xff226622);
    c[static_cast<size_t>(ColourId::TextInactive)]        = C (0xff999aaa);
    c[static_cast<size_t>(ColourId::TextSustainOn)]       = C (0xff007744);
    c[static_cast<size_t>(ColourId::TextFxOverlay)]       = C (0xff224499);
    c[static_cast<size_t>(ColourId::TextPan)]             = C (0xff556677);
    c[static_cast<size_t>(ColourId::TextStagePath)]       = C (0xff556677);
    c[static_cast<size_t>(ColourId::TextSettingsHeader)]  = C (0xffaa5500);

    c[static_cast<size_t>(ColourId::MeterGreen)]          = C (0xff00aa33);
    c[static_cast<size_t>(ColourId::MeterYellow)]         = C (0xffccaa00);
    c[static_cast<size_t>(ColourId::MeterRed)]            = C (0xffcc2200);

    c[static_cast<size_t>(ColourId::BorderDefault)]       = C (0xffaaaacc);
    c[static_cast<size_t>(ColourId::BorderOutline)]       = C (0xffaaaacc);
    c[static_cast<size_t>(ColourId::BorderActive)]        = C (0xff3366aa);

    c[static_cast<size_t>(ColourId::PaletteBlue)]         = C (0xff4682b4);
    c[static_cast<size_t>(ColourId::PaletteRed)]          = C (0xffaa3333);
    c[static_cast<size_t>(ColourId::PaletteGreen)]        = C (0xff338833);
    c[static_cast<size_t>(ColourId::PaletteOrange)]       = C (0xffcc6600);
    c[static_cast<size_t>(ColourId::PaletteViolet)]       = C (0xff7733bb);
    c[static_cast<size_t>(ColourId::PaletteTeal)]         = C (0xff007788);

    c[static_cast<size_t>(ColourId::StageBg)]             = C (0xffddddee);
    c[static_cast<size_t>(ColourId::StageItemNormal)]     = C (0xfff0f0f8);
    c[static_cast<size_t>(ColourId::StageItemSelected)]   = C (0xff6699cc);
    c[static_cast<size_t>(ColourId::StageItemActive)]     = C (0xff66cc66);
    c[static_cast<size_t>(ColourId::StageCtrlNormal)]     = C (0xffd8d8e8);
    c[static_cast<size_t>(ColourId::StageCtrlActive)]     = C (0xff6699cc);

    c[static_cast<size_t>(ColourId::ListBg)]              = C (0xfff0f0f8);
    c[static_cast<size_t>(ColourId::ListOutline)]         = C (0xffaaaacc);

    c[static_cast<size_t>(ColourId::SettingsSelected)]    = C (0xff6699cc);
    c[static_cast<size_t>(ColourId::SettingsRescan)]      = C (0xff44aa44);
    c[static_cast<size_t>(ColourId::SettingsPathText)]    = C (0xff003366);
    c[static_cast<size_t>(ColourId::SettingsSepLine)]     = C (0xffaaaacc);

    c[static_cast<size_t>(ColourId::MetroBeatOff)]        = C (0xffd8d8e8);
    c[static_cast<size_t>(ColourId::MetroBeatBg)]         = C (0xffe8e8f4);
    c[static_cast<size_t>(ColourId::MetroTap)]            = C (0xff6699cc);
    c[static_cast<size_t>(ColourId::MetroBpm)]            = C (0xffd8d8e8);
}

// =============================================================================
// ThemePalette implementation
// =============================================================================

ThemePalette::ThemePalette()
{
    applyDarkDefaults (colours_);
}

ThemePalette& ThemePalette::getInstance()
{
    static ThemePalette instance;
    return instance;
}

juce::Colour ThemePalette::get (ColourId id) noexcept
{
    return getInstance().colours_[static_cast<size_t>(id)];
}

void ThemePalette::resetToDefaults (bool light)
{
    if (light)
        applyLightDefaults (colours_);
    else
        applyDarkDefaults (colours_);
}

void ThemePalette::setByKey (const juce::String& key, juce::uint32 argb)
{
    for (const auto& [name, id] : kKeyMap)
    {
        if (key == name)
        {
            colours_[static_cast<size_t>(id)] = juce::Colour (argb);
            return;
        }
    }
    // Unknown key — silently ignore
}

juce::String ThemePalette::getDisplayName (const juce::File& jsonFile)
{
    if (! jsonFile.existsAsFile())
        return jsonFile.getFileNameWithoutExtension();

    auto parsed = juce::JSON::parse (jsonFile.loadFileAsString());
    if (auto* obj = parsed.getDynamicObject())
    {
        auto prop = obj->getProperties()["_name"];
        if (prop.isString() && prop.toString().isNotEmpty())
            return prop.toString();
    }
    return jsonFile.getFileNameWithoutExtension();
}

void ThemePalette::load (const juce::File& jsonFile)
{
    if (! jsonFile.existsAsFile())
        return;

    auto parsed = juce::JSON::parse (jsonFile.loadFileAsString());
    if (auto* obj = parsed.getDynamicObject())
    {
        for (const auto& prop : obj->getProperties())
        {
            const juce::String& key = prop.name.toString();
            const juce::String  val = prop.value.toString().trim().toLowerCase();

            if (val.length() == 6)
            {
                // RRGGBB  → 0xffRRGGBB
                const juce::uint32 rgb  = static_cast<juce::uint32> (val.getHexValue32());
                const juce::uint32 argb = 0xff000000u | (rgb & 0x00ffffffu);
                setByKey (key, argb);
            }
            else if (val.length() == 8)
            {
                // AARRGGBB
                setByKey (key, static_cast<juce::uint32> (val.getHexValue32()));
            }
        }
    }
}
