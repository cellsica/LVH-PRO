#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "VUPhysicsEngine.h"

// =========================================================================
// VUMeterComponent
// Draws a dual-channel (L + R) analogue VU meter face at 60 fps.
// Geometry: two 190×132 px faces side by side, pivot 30 px below each face.
// =========================================================================
class VUMeterComponent : public juce::Component,
                         private juce::Timer
{
public:
    enum class Theme { VintageWarm, OxygenNeon };

    static constexpr int kW = 390;
    static constexpr int kH = 152;

    explicit VUMeterComponent (VUPhysicsEngine& physics)
        : physics_ (physics)
    {
        setSize (kW, kH);
        startTimerHz (60);
    }

    void setTheme (Theme t) noexcept { theme_ = t; }
    Theme getTheme() const noexcept  { return theme_; }

private:
    // ---- geometry -------------------------------------------------------
    static constexpr float kMeterW    = 190.f;
    static constexpr float kMeterH    = 132.f;
    static constexpr float kGap       =   4.f;   // gap between L and R face
    static constexpr float kPad       =   3.f;   // outer padding
    static constexpr float kLabelH    =  14.f;   // "L" / "R" label area
    static constexpr float kPivotOffY =  30.f;   // pivot below face bottom
    static constexpr float kTickRad   = 108.f;   // radius to outer tick edge
    static constexpr float kLabelRad  =  92.f;   // radius to label centre
    static constexpr float kNeedleLen = 102.f;   // pivot → tip
    static constexpr float kSwingDeg  =  50.f;   // half-angle of full swing

    static constexpr float kMinDb = VUPhysicsEngine::kMinDb;  // -20
    static constexpr float kMaxDb = VUPhysicsEngine::kMaxDb;  //  +3

    // ---- colour palette -------------------------------------------------
    struct Palette { juce::Colour frame, face, needle, redZone, text, zeroDB; };

    Palette palette() const noexcept
    {
        if (theme_ == Theme::VintageWarm)
            return { juce::Colour (0xFF2E2416),   // frame (dark walnut)
                     juce::Colour (0xFFFDF5E6),   // face (cream)
                     juce::Colour (0xFF333333),   // needle
                     juce::Colour (0xFFE63946),   // red zone
                     juce::Colour (0xFF777766),   // scale text
                     juce::Colour (0xFF222222) }; // 0 dB marker
        else // OxygenNeon
            return { juce::Colour (0xFF0A0A14),
                     juce::Colour (0xFF1A1A2A),
                     juce::Colour (0xFFFF8C00),
                     juce::Colour (0xFFFF4400),
                     juce::Colour (0xFF4A6A6A),
                     juce::Colour (0xFFFF8C00) };
    }

    // ---- geometry helpers -----------------------------------------------
    static float dbToPhysAngle (float db) noexcept
    {
        return (db - kMinDb) / (kMaxDb - kMinDb);
    }

    // Maps physics angle [0,1] to a screen point at the given radius.
    static juce::Point<float> toPoint (float physAngle, float radius,
                                       float px, float py) noexcept
    {
        const float deg = juce::jmap (physAngle, 0.f, 1.f, -kSwingDeg, kSwingDeg);
        const float rad = juce::degreesToRadians (deg);
        return { px + radius * std::sin (rad),
                 py - radius * std::cos (rad) };
    }

    // ---- Timer ----------------------------------------------------------
    void timerCallback() override { repaint(); }

    // ---- paint ----------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto pal = palette();
        g.fillAll (pal.frame);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float fx = kPad + ch * (kMeterW + kGap);
            const float fy = kPad;
            drawMeter (g, ch, fx, fy, pal);

            // Channel label (L / R)
            g.setFont (juce::Font (10.f, juce::Font::bold));
            g.setColour (pal.text);
            g.drawText (ch == 0 ? "L" : "R",
                        (int)fx, kH - (int)kLabelH - 1,
                        (int)kMeterW, (int)kLabelH,
                        juce::Justification::centred);
        }
    }

    void drawMeter (juce::Graphics& g, int ch,
                    float fx, float fy, const Palette& pal)
    {
        juce::Rectangle<float> face (fx, fy, kMeterW, kMeterH);
        const float pivotX = fx + kMeterW * 0.5f;
        const float pivotY = fy + kMeterH + kPivotOffY;

        // 1. Face background
        g.setColour (pal.face);
        g.fillRoundedRectangle (face, 5.f);

        // 2. Red zone arc band (0 dB → +3 dB)
        drawRedArc (g, pivotX, pivotY, pal.redZone);

        // 3. Scale ticks + dB labels
        drawScale (g, pivotX, pivotY, pal);

        // 4. Needle
        const float physAngle = juce::jlimit (0.f, 1.15f,
                                              physics_.getNeedleAngle (ch));
        const auto tip = toPoint (physAngle, kNeedleLen, pivotX, pivotY);

        g.setColour (juce::Colours::black.withAlpha (0.20f));  // shadow
        g.drawLine (pivotX + 1.f, pivotY + 1.f,
                    tip.x  + 1.f, tip.y  + 1.f, 1.8f);
        g.setColour (pal.needle);
        g.drawLine (pivotX, pivotY, tip.x, tip.y, 1.8f);
        g.fillEllipse (pivotX - 3.5f, pivotY - 3.5f, 7.f, 7.f); // pivot dot

        // 5. Glass sheen — top 45% of face, white gradient
        {
            juce::ColourGradient sheen (
                juce::Colours::white.withAlpha (0.22f), fx, fy,
                juce::Colours::white.withAlpha (0.00f), fx, fy + kMeterH * 0.45f,
                false);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (face.toNearestInt());
            g.setGradientFill (sheen);
            g.fillRoundedRectangle (face, 5.f);
        }

        // 6. Border
        g.setColour (pal.frame.brighter (0.25f));
        g.drawRoundedRectangle (face, 5.f, 1.5f);
    }

    // Filled arc band marking the red zone (0 dB to +3 dB).
    void drawRedArc (juce::Graphics& g, float px, float py,
                     juce::Colour col) const
    {
        const float zeroA = dbToPhysAngle (0.f);   // 0 dB physAngle ≈ 0.87
        const float maxA  = 1.f;                   // +3 dB physAngle

        const float r1 = kTickRad - 14.f;
        const float r2 = kTickRad - 1.f;

        g.setColour (col.withAlpha (0.30f));
        const int steps = 16;
        for (int i = 0; i < steps; ++i)
        {
            const float t1 = juce::jmap ((float)i,       0.f, (float)steps, zeroA, maxA);
            const float t2 = juce::jmap ((float)(i + 1), 0.f, (float)steps, zeroA, maxA);
            const auto  a  = toPoint (t1, r1, px, py);
            const auto  b  = toPoint (t1, r2, px, py);
            const auto  c  = toPoint (t2, r2, px, py);
            const auto  d  = toPoint (t2, r1, px, py);

            juce::Path seg;
            seg.addQuadrilateral (a.x, a.y, b.x, b.y, c.x, c.y, d.x, d.y);
            g.fillPath (seg);
        }
    }

    // Tick marks and dB labels along the scale arc.
    void drawScale (juce::Graphics& g, float px, float py,
                    const Palette& pal) const
    {
        struct Mark { float db; bool major; };
        static const Mark kMarks[] = {
            { -20.f, true  }, { -10.f, true  }, {  -7.f, false },
            {  -5.f, false }, {  -3.f, true  }, {  -2.f, false },
            {  -1.f, false }, {   0.f, true  }, {  +1.f, false },
            {  +2.f, false }, {  +3.f, true  }
        };

        for (const auto& m : kMarks)
        {
            const float pa    = dbToPhysAngle (m.db);
            const float inner = kTickRad - (m.major ? 10.f : 6.f);
            const auto  p1    = toPoint (pa, kTickRad, px, py);
            const auto  p2    = toPoint (pa, inner,    px, py);

            const bool isRed  = m.db >  0.f;
            const bool isZero = m.db == 0.f;

            g.setColour (isRed ? pal.redZone
                               : (isZero ? pal.zeroDB : pal.text));
            g.drawLine (p1.x, p1.y, p2.x, p2.y,
                        isZero ? 2.2f : (m.major ? 1.5f : 1.0f));

            // dB number (major ticks only)
            if (m.major)
            {
                const auto lp = toPoint (pa, kLabelRad, px, py);
                const juce::String lbl = (m.db > 0.f)
                    ? ("+" + juce::String ((int)m.db))
                    : juce::String ((int)m.db);
                g.setFont (juce::Font (8.5f));
                g.setColour (isRed ? pal.redZone : pal.text);
                g.drawText (lbl,
                            (int)(lp.x - 13), (int)(lp.y - 7),
                            26, 14,
                            juce::Justification::centred);
            }
        }
    }

    // ---- right-click context menu ---------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
            showContextMenu();
    }

    void showContextMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Backlight");
        menu.addItem (1, "Vintage Warm", true, theme_ == Theme::VintageWarm);
        menu.addItem (2, "Oxygen Neon",  true, theme_ == Theme::OxygenNeon);
        menu.showMenuAsync ({}, [this] (int r) {
            if (r == 1) theme_ = Theme::VintageWarm;
            if (r == 2) theme_ = Theme::OxygenNeon;
        });
    }

    VUPhysicsEngine& physics_;
    Theme            theme_ = Theme::VintageWarm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VUMeterComponent)
};


// =========================================================================
// VUMeterWindow
// Hosts VUMeterComponent in a native window.
// Phase D will convert this to a frameless / click-through Win32 window.
// =========================================================================
class VUMeterWindow : public juce::DocumentWindow
{
public:
    explicit VUMeterWindow (VUPhysicsEngine& physics)
        : juce::DocumentWindow ("VU Meter",
                                juce::Colour (0xFF2E2416),
                                juce::DocumentWindow::closeButton)
    {
        auto* comp = new VUMeterComponent (physics);
        component_ = comp;
        setUsingNativeTitleBar (true);
        setResizable (false, false);
        setContentOwned (comp, true);
        setVisible (false);
    }

    void closeButtonPressed() override { setVisible (false); }

    VUMeterComponent& getComponent() noexcept { return *component_; }

private:
    VUMeterComponent* component_ = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VUMeterWindow)
};
