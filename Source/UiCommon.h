#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <memory>
#include <map>
#include <set>
#include <atomic>

using namespace juce;

// PC keyboard → MIDI note (standard convention: MIDI 60 = C4; Z = C5 = MIDI 72)
inline int pcKeyToNote (int kc)
{
    switch (kc)
    {
        case 'z': case 'Z': return 72;
        case 's': case 'S': return 73;
        case 'x': case 'X': return 74;
        case 'd': case 'D': return 75;
        case 'c': case 'C': return 76;
        case 'v': case 'V': return 77;
        case 'g': case 'G': return 78;
        case 'b': case 'B': return 79;
        case 'h': case 'H': return 80;
        case 'n': case 'N': return 81;
        case 'j': case 'J': return 82;
        case 'm': case 'M': return 83;
        case ',':            return 84;
        default:             return -1;
    }
}

inline const std::map<int, String>& noteKeyLabels()
{
    static const std::map<int, String> m {
        {72,"Z"},{73,"S"},{74,"X"},{75,"D"},{76,"C"},
        {77,"V"},{78,"G"},{79,"B"},{80,"H"},{81,"N"},
        {82,"J"},{83,"M"},{84,","}
    };
    return m;
}

// =====================================================================
// Icon draw functions
// =====================================================================
namespace Icons
{
    inline void preset (Graphics& g, Rectangle<float> a)
    {
        g.setColour (Colours::white);
        g.fillRoundedRectangle (a, 1.5f);
        g.setColour (Colour (0xff252535));
        g.fillRect (a.withTrimmedTop (a.getHeight() * 0.57f).reduced (1.5f, 0.f));
        g.fillRect (a.getRight() - 5.f, a.getY(), 4.f, 4.5f);
    }

    inline void load (Graphics& g, Rectangle<float> a)
    {
        g.setColour (Colours::white);
        float cx = a.getCentreX(), topH = a.getHeight() * 0.55f, aw = a.getWidth() * 0.35f;
        g.fillRect (cx - 1.5f, a.getY(), 3.f, topH - 3.f);
        Path arrow;
        arrow.addTriangle (cx - aw, a.getY() + topH - 3.f,
                           cx + aw, a.getY() + topH - 3.f,
                           cx,      a.getY() + topH + 4.f);
        g.fillPath (arrow);
        g.fillRect (a.getX(), a.getBottom() - 2.5f, a.getWidth(), 2.5f);
    }

    inline void unload (Graphics& g, Rectangle<float> a)
    {
        g.setColour (Colours::white);
        float cx = a.getCentreX(), triH = a.getHeight() * 0.55f;
        Path tri;
        tri.addTriangle (cx, a.getY(), a.getRight(), a.getY() + triH, a.getX(), a.getY() + triH);
        g.fillPath (tri);
        g.fillRect (a.getX(), a.getY() + triH + 2.f, a.getWidth(), 3.f);
    }

    inline void panic (Graphics& g, Rectangle<float> a)
    {
        g.setColour (Colours::white);
        float cx = a.getCentreX(), w = jmax (3.f, a.getWidth() * 0.30f), dot = w * 0.55f;
        g.fillRoundedRectangle (cx - w * 0.5f, a.getY(), w, a.getHeight() * 0.62f, 1.5f);
        g.fillEllipse (cx - dot, a.getBottom() - dot * 2.f, dot * 2.f, dot * 2.f);
    }

    inline void kbd (Graphics& g, Rectangle<float> a)
    {
        float kw = a.getWidth() / 3.5f, gap = (a.getWidth() - 3.f * kw) / 2.f;
        g.setColour (Colours::white);
        for (int i = 0; i < 3; ++i)
            g.fillRoundedRectangle (a.getX() + i * (kw + gap), a.getY(), kw - 0.5f, a.getHeight(), 1.f);
        float bw = kw * 0.55f, bh = a.getHeight() * 0.60f;
        g.setColour (Colour (0xff252535));
        g.fillRect (a.getX() +       (kw + gap) - bw * 0.5f, a.getY(), bw, bh);
        g.fillRect (a.getX() + 2.f * (kw + gap) - bw * 0.5f, a.getY(), bw, bh);
    }

    inline void monitor (Graphics& g, Rectangle<float> a)
    {
        g.setColour (Colours::white);
        float barH = jmax (2.f, a.getHeight() * 0.13f);
        float gap  = (a.getHeight() - 3.f * barH) / 4.f;
        for (int i = 0; i < 3; ++i)
            g.fillRoundedRectangle (a.getX(), a.getY() + gap * (i + 1) + barH * i,
                                    a.getWidth(), barH, 1.f);
    }

    inline void mixer (Graphics& g, Rectangle<float> a)
    {
        g.setColour (Colours::white);
        float w = a.getWidth() / 4.0f;
        float h = a.getHeight();
        for (int i = 0; i < 3; ++i)
        {
            float x = a.getX() + (i + 1) * w;
            g.drawVerticalLine (roundToInt(x), a.getY(), a.getBottom());
            float thumbY = a.getY() + h * (0.3f + i * 0.2f);
            g.fillRect (x - 2.5f, thumbY - 1.5f, 5.0f, 3.0f);
        }
    }

    // Push-pin — head circle + horizontal bar + shaft + point
    inline void pin (Graphics& g, Rectangle<float> a)
    {
        g.setColour (Colours::white);
        float cx = a.getCentreX();

        // Head (circle)
        float hr = a.getWidth() * 0.20f;
        g.fillEllipse (cx - hr, a.getY() + a.getHeight() * 0.04f, hr * 2.f, hr * 2.f);

        // Horizontal bar
        float barW = a.getWidth() * 0.68f;
        float barH = a.getHeight() * 0.11f;
        float barY = a.getY() + a.getHeight() * 0.32f;
        g.fillRoundedRectangle (cx - barW * 0.5f, barY, barW, barH, 1.f);

        // Shaft
        float shW = a.getWidth() * 0.13f;
        float shY = barY + barH;
        float shH = a.getHeight() * 0.35f;
        g.fillRoundedRectangle (cx - shW * 0.5f, shY, shW, shH, 1.f);

        // Point (downward triangle)
        Path p;
        p.addTriangle (cx - shW * 0.5f, shY + shH,
                       cx + shW * 0.5f, shY + shH,
                       cx,              shY + shH + a.getHeight() * 0.13f);
        g.fillPath (p);
    }

    // Stage setlist — play triangle + 3 horizontal list lines
    inline void stage (Graphics& g, Rectangle<float> a)
    {
        g.setColour (Colours::white);

        // Play triangle (left side)
        float ts = a.getHeight() * 0.22f;
        float tx = a.getX() + a.getWidth() * 0.06f;
        float ty = a.getCentreY();
        Path p;
        p.addTriangle (tx, ty - ts, tx, ty + ts, tx + ts * 1.4f, ty);
        g.fillPath (p);

        // Three list lines (right side)
        float barH  = a.getHeight() * 0.10f;
        float gap   = (a.getHeight() - 3.f * barH) / 4.f;
        float lineX = a.getX() + a.getWidth() * 0.38f;
        float lineW = a.getWidth() * 0.58f;
        for (int i = 0; i < 3; ++i)
            g.fillRoundedRectangle (lineX, a.getY() + gap * (i + 1) + barH * i,
                                    lineW, barH, 1.f);
    }

    // Mini LED meter — two vertical bars (L/R) with green/yellow/red segments
    inline void led (Graphics& g, Rectangle<float> a)
    {
        const float barW = a.getWidth() * 0.30f;
        const float gap  = a.getWidth() * 0.08f;
        const float cx   = a.getCentreX();
        const float h    = a.getHeight();

        for (int bar = 0; bar < 2; ++bar)
        {
            float x = (bar == 0) ? cx - barW - gap * 0.5f : cx + gap * 0.5f;

            // Red top (clipping zone)
            g.setColour (Colour (0xffff3333));
            g.fillRect (x, a.getY(),              barW, h * 0.20f - 1.f);
            // Yellow mid
            g.setColour (Colour (0xffffcc00));
            g.fillRect (x, a.getY() + h * 0.22f, barW, h * 0.25f - 1.f);
            // Green bottom
            g.setColour (Colour (0xff00dd44));
            g.fillRect (x, a.getY() + h * 0.50f, barW, h * 0.50f);
        }
    }
}

// =====================================================================
// LvhLookAndFeel
//
// Application-wide LookAndFeel that replaces the JUCE default fonts
// with Japanese-capable alternatives on Windows 10/11:
//
//   Default sans-serif  →  "Yu Gothic UI"  (system UI font, W10/11)
//   Default monospace   →  "MS Gothic"     (Japanese-capable fixed-width)
//
// Applied globally in LvhProApplication::initialise() so that ALL
// components — Labels, Buttons, AlertWindows, custom paint() calls
// using Font(size, style) — render Japanese (and other CJK) text
// without relying on font-fallback mechanisms that JUCE may not invoke.
// =====================================================================
class LvhLookAndFeel : public LookAndFeel_V4
{
public:
    Typeface::Ptr getTypefaceForFont (const Font& f) override
    {
        const auto& name = f.getTypefaceName();

        if (name == Font::getDefaultSansSerifFontName())
            return LookAndFeel_V4::getTypefaceForFont (
                Font ("Yu Gothic UI", f.getHeight(), f.getStyleFlags()));

        if (name == Font::getDefaultMonospacedFontName())
            return LookAndFeel_V4::getTypefaceForFont (
                Font ("MS Gothic", f.getHeight(), f.getStyleFlags()));

        return LookAndFeel_V4::getTypefaceForFont (f);
    }
};
