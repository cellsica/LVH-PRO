#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "VUPhysicsEngine.h"

// =========================================================================
// VUMeterComponent
// Draws a dual-channel (L + R) analogue VU meter face at 60 fps.
// Geometry: two 190×132 px faces side by side, pivot 30 px below each face.
//
// Interaction (Phase D):
//   Bottom kHandleH px strip is the only interactive area (left-drag to move,
//   right-click for backlight menu).  The rest of the window is click-through
//   via Win32 NCHITTEST.
// =========================================================================
class VUMeterComponent : public juce::Component,
                         private juce::Timer
{
public:
    enum class Theme { VintageWarm, OxygenNeon };

    static constexpr int kW       = 390;
    static constexpr int kH       = 152;
    static constexpr int kHandleH =  16;   // interactive strip at bottom

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
    static constexpr float kGap       =   4.f;
    static constexpr float kPad       =   3.f;
    static constexpr float kLabelH    =  14.f;
    static constexpr float kPivotOffY =  30.f;
    static constexpr float kTickRad   = 108.f;
    static constexpr float kLabelRad  =  92.f;
    static constexpr float kNeedleLen = 102.f;
    static constexpr float kSwingDeg  =  50.f;

    static constexpr float kMinDb = VUPhysicsEngine::kMinDb;
    static constexpr float kMaxDb = VUPhysicsEngine::kMaxDb;

    // ---- colour palette -------------------------------------------------
    struct Palette { juce::Colour frame, face, needle, redZone, text, zeroDB; };

    Palette palette() const noexcept
    {
        if (theme_ == Theme::VintageWarm)
            return { juce::Colour (0xFF2E2416),
                     juce::Colour (0xFFFDF5E6),
                     juce::Colour (0xFF333333),
                     juce::Colour (0xFFE63946),
                     juce::Colour (0xFF777766),
                     juce::Colour (0xFF222222) };
        else
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
            drawMeter (g, ch, fx, kPad, pal);

            // Channel label
            g.setFont (juce::Font (10.f, juce::Font::bold));
            g.setColour (pal.text);
            g.drawText (ch == 0 ? "L" : "R",
                        (int)fx, kH - (int)kLabelH - 1,
                        (int)kMeterW, (int)kLabelH,
                        juce::Justification::centred);
        }

        // Drag handle indicator — three dots centred at the bottom strip
        drawDragHandle (g, pal);
    }

    void drawDragHandle (juce::Graphics& g, const Palette& pal)
    {
        const int cy   = kH - kHandleH / 2;
        const int cx   = kW / 2;
        g.setColour (pal.text.withAlpha (0.55f));
        for (int i = -1; i <= 1; ++i)
            g.fillEllipse ((float)(cx + i * 6 - 1), (float)(cy - 1), 3.f, 3.f);
    }

    void drawMeter (juce::Graphics& g, int ch,
                    float fx, float fy, const Palette& pal)
    {
        juce::Rectangle<float> face (fx, fy, kMeterW, kMeterH);
        const float pivotX = fx + kMeterW * 0.5f;
        const float pivotY = fy + kMeterH + kPivotOffY;

        // Face
        g.setColour (pal.face);
        g.fillRoundedRectangle (face, 5.f);

        // Red zone arc
        drawRedArc (g, pivotX, pivotY, pal.redZone);

        // Scale
        drawScale (g, pivotX, pivotY, pal);

        // Needle
        const float physAngle = juce::jlimit (0.f, 1.15f,
                                              physics_.getNeedleAngle (ch));
        const auto tip = toPoint (physAngle, kNeedleLen, pivotX, pivotY);

        g.setColour (juce::Colours::black.withAlpha (0.20f));
        g.drawLine (pivotX + 1.f, pivotY + 1.f, tip.x + 1.f, tip.y + 1.f, 1.8f);
        g.setColour (pal.needle);
        g.drawLine (pivotX, pivotY, tip.x, tip.y, 1.8f);
        g.fillEllipse (pivotX - 3.5f, pivotY - 3.5f, 7.f, 7.f);

        // Glass sheen
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

        // Border
        g.setColour (pal.frame.brighter (0.25f));
        g.drawRoundedRectangle (face, 5.f, 1.5f);
    }

    void drawRedArc (juce::Graphics& g, float px, float py,
                     juce::Colour col) const
    {
        const float zeroA = dbToPhysAngle (0.f);
        const float r1    = kTickRad - 14.f;
        const float r2    = kTickRad -  1.f;

        g.setColour (col.withAlpha (0.30f));
        const int steps = 16;
        for (int i = 0; i < steps; ++i)
        {
            const float t1 = juce::jmap ((float)i,       0.f, (float)steps, zeroA, 1.f);
            const float t2 = juce::jmap ((float)(i + 1), 0.f, (float)steps, zeroA, 1.f);
            const auto  a  = toPoint (t1, r1, px, py);
            const auto  b  = toPoint (t1, r2, px, py);
            const auto  c  = toPoint (t2, r2, px, py);
            const auto  d  = toPoint (t2, r1, px, py);
            juce::Path seg;
            seg.addQuadrilateral (a.x, a.y, b.x, b.y, c.x, c.y, d.x, d.y);
            g.fillPath (seg);
        }
    }

    void drawScale (juce::Graphics& g, float px, float py,
                    const Palette& pal) const
    {
        struct Mark { float db; bool major; };
        static const Mark kMarks[] = {
            { -40.f, true  }, { -30.f, true  }, { -20.f, true  },
            { -10.f, true  }, {  -7.f, false }, {  -5.f, false },
            {  -3.f, true  }, {  -2.f, false }, {  -1.f, false },
            {   0.f, true  }, {  +1.f, false }, {  +2.f, false },
            {  +3.f, true  }
        };

        for (const auto& m : kMarks)
        {
            const float pa    = dbToPhysAngle (m.db);
            const float inner = kTickRad - (m.major ? 10.f : 6.f);
            const auto  p1    = toPoint (pa, kTickRad, px, py);
            const auto  p2    = toPoint (pa, inner,    px, py);

            const bool isRed  = m.db >  0.f;
            const bool isZero = m.db == 0.f;

            g.setColour (isRed  ? pal.redZone
                       : isZero ? pal.zeroDB : pal.text);
            g.drawLine (p1.x, p1.y, p2.x, p2.y,
                        isZero ? 2.2f : (m.major ? 1.5f : 1.0f));

            if (m.major)
            {
                const auto lp  = toPoint (pa, kLabelRad, px, py);
                const juce::String lbl = (m.db > 0.f)
                    ? ("+" + juce::String ((int)m.db))
                    : juce::String ((int)m.db);
                g.setFont (juce::Font (8.5f));
                g.setColour (isRed ? pal.redZone : pal.text);
                g.drawText (lbl, (int)(lp.x - 13), (int)(lp.y - 7),
                            26, 14, juce::Justification::centred);
            }
        }
    }

    // ---- mouse: drag (left) + menu (right) — only fired from kHandleH strip
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isLeftButtonDown())
            dragger_.startDraggingComponent (getTopLevelComponent(), e);
        else if (e.mods.isRightButtonDown())
            showContextMenu();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isLeftButtonDown())
            dragger_.dragComponent (getTopLevelComponent(), e, nullptr);
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

    VUPhysicsEngine&     physics_;
    Theme                theme_ = Theme::VintageWarm;
    juce::ComponentDragger dragger_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VUMeterComponent)
};


// =========================================================================
// VUMeterWindow
// DocumentWindow that hosts VUMeterComponent.
// Extends the proven DocumentWindow pattern used by MixerWindow /
// MetronomeWindow in this project, then applies Win32 overlay styles:
//   - Always-on-top (via JUCE setAlwaysOnTop)
//   - 90% opacity (WS_EX_LAYERED / SetLayeredWindowAttributes)
//   - Click-through (WM_NCHITTEST subclass):
//       non-client area (title bar) → handled by original proc as-is
//       client area, bottom kHandleH px → HTCLIENT (drag + right-click)
//       client area, rest → HTTRANSPARENT (click-through)
// =========================================================================
class VUMeterWindow : public juce::DocumentWindow
{
public:
    // Fired when the user clicks the window's close button.
    // Wire this to update the toolbar toggle button state.
    std::function<void()> onClose;

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
        centreWithSize (VUMeterComponent::kW, VUMeterComponent::kH);
        setAlwaysOnTop (true);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClose) onClose();
    }

    void show()
    {
        setVisible (true);
        toFront (false);
        applyWin32Styles();   // idempotent — installs once
    }

    void hide() { setVisible (false); }

    // opacity in [0.0, 1.0]. Applied immediately if the Win32 handle is live.
    void setOpacity (float opacity);

    VUMeterComponent& getComponent() noexcept { return *component_; }

private:
    void applyWin32Styles();   // implemented in VUMeterWindow.cpp
    void uninstallWndProc();

    VUMeterComponent* component_  = nullptr;
    void*             nativeHwnd_ = nullptr;   // non-null after first applyWin32Styles()
    float             opacity_    = 0.9f;      // [0.0, 1.0], applied via WS_EX_LAYERED

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VUMeterWindow)
};
