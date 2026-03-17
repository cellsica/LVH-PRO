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

// PC keyboard → MIDI note (channel 1, middle C = MIDI 60 = C4)
inline int pcKeyToNote (int kc)
{
    switch (kc)
    {
        case 'z': case 'Z': return 60;
        case 's': case 'S': return 61;
        case 'x': case 'X': return 62;
        case 'd': case 'D': return 63;
        case 'c': case 'C': return 64;
        case 'v': case 'V': return 65;
        case 'g': case 'G': return 66;
        case 'b': case 'B': return 67;
        case 'h': case 'H': return 68;
        case 'n': case 'N': return 69;
        case 'j': case 'J': return 70;
        case 'm': case 'M': return 71;
        case ',':            return 72;
        default:             return -1;
    }
}

inline const std::map<int, String>& noteKeyLabels()
{
    static const std::map<int, String> m {
        {60,"Z"},{61,"S"},{62,"X"},{63,"D"},{64,"C"},
        {65,"V"},{66,"G"},{67,"B"},{68,"H"},{69,"N"},
        {70,"J"},{71,"M"},{72,","}
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
}
