#pragma once
#include <JuceHeader.h>
#include "../Core/ThemePalette.h"

/**
 * @file VirtualLayoutComponent.h
 * @brief Virtual keyboard + pad grid display for Instrument Layout Studio.
 *
 * @note Mission 055 Phase B
 *
 * Layout (top → bottom):
 *   - Pad grid (shown only when numPads_ > 0)
 *   - MidiKeyboardComponent (always shown)
 *
 * MIDI Feedback:
 *   Call handleMidiMessage() from any thread — it is forwarded to the
 *   thread-safe juce::MidiKeyboardState which causes the keyboard to repaint.
 */
class VirtualLayoutComponent : public juce::Component
{
public:
    // ── Key-range presets (first note, last note, white-key count) ────────────
    struct KeyRange { int lo; int hi; int numWhite; const char* label; };
    static constexpr KeyRange kRanges[] = {
        { 36,  60, 15, "25" },   // C2 – C4
        { 24,  72, 29, "49" },   // C1 – C5
        { 24,  84, 36, "61" },   // C1 – C6
        { 21, 108, 52, "88" },   // A0 – C8
    };
    static constexpr int kDefaultRangeIndex = 2;  // 61 keys

    // ── Construction ──────────────────────────────────────────────────────────
    VirtualLayoutComponent()
        : keyboard_ (keyboardState_,
                     juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        applyThemeToKeyboard();
        keyboard_.setScrollButtonsVisible (false);
        keyboard_.setLowestVisibleKey (kRanges[kDefaultRangeIndex].lo);
        keyboard_.setAvailableRange (kRanges[kDefaultRangeIndex].lo,
                                     kRanges[kDefaultRangeIndex].hi);
        addAndMakeVisible (keyboard_);
        setNumPads (16);
        setRangeIndex (kDefaultRangeIndex);
    }

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * @brief Set the keyboard range by preset index (0=25, 1=49, 2=61, 3=88).
     * Must be called from the message thread.
     */
    void setRangeIndex (int idx)
    {
        rangeIndex_ = juce::jlimit (0, 3, idx);
        const auto& r = kRanges[rangeIndex_];
        keyboard_.setAvailableRange (r.lo, r.hi);
        keyboard_.setLowestVisibleKey (r.lo);
        resized();
    }

    int getRangeIndex() const noexcept { return rangeIndex_; }

    /**
     * @brief Set the number of pads to display (0 = hidden, 4 / 8 / 16).
     * Must be called from the message thread.
     */
    void setNumPads (int n)
    {
        numPads_ = (n == 4 || n == 8 || n == 16) ? n : 0;
        resized();
        repaint();
    }

    int getNumPads() const noexcept { return numPads_; }

    /**
     * @brief Forward a MIDI message for visual feedback.
     * Thread-safe — may be called from the MIDI input thread.
     */
    void handleMidiMessage (const juce::MidiMessage& msg)
    {
        keyboardState_.processNextMidiEvent (msg);
    }

    // ── Preferred size helpers ────────────────────────────────────────────────
    int preferredWidth()  const noexcept { return 700; }
    int preferredHeight() const noexcept
    {
        return kKbdH + (numPads_ > 0 ? kPadAreaH + kPadGap : 0);
    }

    // ── Component overrides ───────────────────────────────────────────────────
    void resized() override
    {
        auto area = getLocalBounds();

        if (numPads_ > 0)
            area.removeFromTop (kPadAreaH + kPadGap);

        // Keyboard fills remaining area; dynamic key-width to span full width.
        keyboard_.setBounds (area);
        const auto& r = kRanges[rangeIndex_];
        float keyW = (float) area.getWidth() / (float) r.numWhite;
        keyboard_.setKeyWidth (juce::jmax (8.0f, keyW));
        keyboard_.setBlackNoteLengthProportion (0.62f);
    }

    void paint (juce::Graphics& g) override
    {
        if (numPads_ <= 0) return;
        paintPadGrid (g);
    }

private:
    // ── Constants ─────────────────────────────────────────────────────────────
    static constexpr int kKbdH     = 100;
    static constexpr int kPadAreaH = 80;
    static constexpr int kPadGap   = 4;

    // ── State ─────────────────────────────────────────────────────────────────
    juce::MidiKeyboardState      keyboardState_;
    juce::MidiKeyboardComponent  keyboard_;
    int                          rangeIndex_ = kDefaultRangeIndex;
    int                          numPads_    = 16;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void applyThemeToKeyboard()
    {
        using ID = juce::MidiKeyboardComponent;
        keyboard_.setColour (ID::whiteNoteColourId,
                             ThemePalette::get (ColourId::KeyWhite));
        keyboard_.setColour (ID::blackNoteColourId,
                             ThemePalette::get (ColourId::KeyBlack));
        keyboard_.setColour (ID::keyDownOverlayColourId,
                             ThemePalette::get (ColourId::KeyNoteActive));
        keyboard_.setColour (ID::mouseOverKeyOverlayColourId,
                             ThemePalette::get (ColourId::KeyNoteActive).withAlpha (0.4f));
        keyboard_.setColour (ID::upDownButtonArrowColourId,
                             ThemePalette::get (ColourId::TextPrimary));
        keyboard_.setColour (ID::upDownButtonBackgroundColourId,
                             ThemePalette::get (ColourId::BgPanel));
    }

    void paintPadGrid (juce::Graphics& g)
    {
        // Grid layout: always 4 columns, rows = numPads / 4
        const int cols = 4;
        const int rows = numPads_ / cols;

        auto area = getLocalBounds().removeFromTop (kPadAreaH).reduced (4, 2);
        const float cellW = (float) area.getWidth()  / cols;
        const float cellH = (float) area.getHeight() / rows;
        const float pad   = 3.0f;

        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                auto cell = juce::Rectangle<float> (
                    area.getX() + c * cellW + pad,
                    area.getY() + r * cellH + pad,
                    cellW - pad * 2.0f,
                    cellH - pad * 2.0f);

                g.setColour (ThemePalette::get (ColourId::BgPanelAlt));
                g.fillRoundedRectangle (cell, 4.0f);

                g.setColour (ThemePalette::get (ColourId::BorderDefault));
                g.drawRoundedRectangle (cell, 4.0f, 1.0f);

                // Pad number label
                g.setColour (ThemePalette::get (ColourId::TextSecondary));
                g.setFont (juce::Font (10.0f));
                g.drawText (juce::String (r * cols + c + 1), cell,
                            juce::Justification::centred, false);
            }
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualLayoutComponent)
};
