#pragma once
#include <JuceHeader.h>
#include <functional>
#include <utility>
#include <vector>
#include "../Core/ThemePalette.h"
#include "../Core/KeyboardBlock.h"

/**
 * @file VirtualLayoutComponent.h
 * @brief Virtual keyboard + pad grid with block-based split/layer editor.
 *
 * @note Mission 055 Phase B / Mission 056 Phase C
 *
 * Layout (top to bottom):
 *   - Pad grid (shown only when numPads_ > 0)
 *   - MidiKeyboardComponent with KeyboardBlock overlays
 *
 * MIDI Feedback:
 *   Call handleMidiMessage() from any thread — forwarded to the thread-safe
 *   juce::MidiKeyboardState which causes the keyboard to repaint.
 *
 * Block Editor:
 *   - Drag empty area       → create block
 *   - Drag block edge       → resize (Ctrl = octave snap)
 *   - Drag block body       → move
 *   - Right-click block     → context menu (assign bridge / octave shift / delete)
 *   - Overlapping blocks    → blended transparency indicates layering
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

    // ── Layout editor callbacks ───────────────────────────────────────────────

    /**
     * @brief Fired on the message thread whenever the block list or pad assignments change.
     *        Wire to MidiRoutingManager::setBlocks() + setPadAssignments() via UIManager.
     */
    std::function<void(const std::vector<KeyboardBlock>&,
                       const std::vector<PadAssignment>&)> onLayoutChanged;

    /**
     * @brief Return the current bridge list as {displayName, pluginPath} pairs.
     *        Called from the message thread when a context menu opens.
     */
    std::function<std::vector<std::pair<juce::String, juce::String>>()> getBridgeList;

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
        // Pass all mouse events to VirtualLayoutComponent; the block editor
        // handles interaction — the default key-click-to-play is not needed.
        keyboard_.setInterceptsMouseClicks (false, false);
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

    /** @brief Replace the block list (e.g. when restoring from a project file). */
    void setBlocks (std::vector<KeyboardBlock> blocks)
    {
        blocks_ = std::move (blocks);
        repaint();
    }

    const std::vector<KeyboardBlock>& getBlocks() const noexcept { return blocks_; }

    /** @brief Replace the pad assignment list (e.g. when restoring from a project file). */
    void setPadAssignments (std::vector<PadAssignment> pads)
    {
        padAssignments_ = std::move (pads);
        repaint();
    }

    const std::vector<PadAssignment>& getPadAssignments() const noexcept { return padAssignments_; }

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
        if (numPads_ > 0) area.removeFromTop (kPadAreaH + kPadGap);

        keyboard_.setBounds (area);
        const auto& r = kRanges[rangeIndex_];
        float keyW = (float) area.getWidth() / (float) r.numWhite;
        keyboard_.setKeyWidth (juce::jmax (8.0f, keyW));
        keyboard_.setBlackNoteLengthProportion (0.62f);
    }

    void paint (juce::Graphics& g) override
    {
        if (numPads_ > 0) paintPadGrid (g);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        paintBlockOverlays (g);
    }

    // ── Mouse — block editor ──────────────────────────────────────────────────

    void mouseMove (const juce::MouseEvent& e) override
    {
        updateCursorForPosition (e.position);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // ── Pad area (top of component) ───────────────────────────────────────
        if (numPads_ > 0 && e.mods.isRightButtonDown())
        {
            auto padBounds = getLocalBounds().removeFromTop (kPadAreaH).reduced (4, 2);
            if (padBounds.toFloat().contains (e.position))
            {
                const int cols  = 4;
                const int rows  = numPads_ / cols;
                const float cellW = (float) padBounds.getWidth()  / cols;
                const float cellH = (float) padBounds.getHeight() / rows;
                int col = juce::jlimit (0, cols - 1,
                              (int) ((e.position.x - padBounds.getX()) / cellW));
                int row = juce::jlimit (0, rows - 1,
                              (int) ((e.position.y - padBounds.getY()) / cellH));
                showPadContextMenu (row * cols + col, e.getScreenPosition());
                return;
            }
        }

        if (! keyboard_.getBounds().toFloat().contains (e.position))
            return;

        if (e.mods.isRightButtonDown())
        {
            // Right-click: find topmost block and show context menu
            for (int i = (int) blocks_.size() - 1; i >= 0; --i)
            {
                if (blockRect (blocks_[i]).contains (e.position.x, e.position.y))
                {
                    showBlockContextMenu (i, e.getScreenPosition());
                    return;
                }
            }
            return;
        }

        static constexpr float kEdge = 7.0f;

        // Left-click: check for resize edge, move body, or empty-area create
        for (int i = (int) blocks_.size() - 1; i >= 0; --i)
        {
            auto r = blockRect (blocks_[i]);
            if (! r.contains (e.position.x, e.position.y))
                continue;

            if (e.position.x <= r.getX() + kEdge)
            {
                editMode_     = EditMode::ResizingStart;
                dragBlockIdx_ = i;
                return;
            }
            if (e.position.x >= r.getRight() - kEdge)
            {
                editMode_     = EditMode::ResizingEnd;
                dragBlockIdx_ = i;
                return;
            }
            editMode_      = EditMode::MovingBlock;
            dragBlockIdx_  = i;
            dragStartNote_ = noteAtX (e.position.x);
            dragOrigStart_ = blocks_[i].startNote;
            dragOrigEnd_   = blocks_[i].endNote;
            return;
        }

        // Empty area — create a new block
        dragAnchorNote_ = noteAtX (e.position.x);
        KeyboardBlock newBlock;
        newBlock.startNote   = dragAnchorNote_;
        newBlock.endNote     = dragAnchorNote_;
        newBlock.blockColour = colourForIndex ((int) blocks_.size());
        blocks_.push_back (newBlock);
        dragBlockIdx_ = (int) blocks_.size() - 1;
        editMode_     = EditMode::CreatingBlock;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (editMode_ == EditMode::Idle) return;

        const auto& r = kRanges[rangeIndex_];
        bool ctrlHeld = e.mods.isCtrlDown();
        int  note     = juce::jlimit (r.lo, r.hi, snapNote (noteAtX (e.position.x), ctrlHeld));

        auto& b = blocks_[dragBlockIdx_];
        switch (editMode_)
        {
            case EditMode::CreatingBlock:
                b.startNote = juce::jmin (dragAnchorNote_, note);
                b.endNote   = juce::jmax (dragAnchorNote_, note);
                break;

            case EditMode::ResizingStart:
                b.startNote = juce::jmin (note, b.endNote);
                break;

            case EditMode::ResizingEnd:
                b.endNote = juce::jmax (note, b.startNote);
                break;

            case EditMode::MovingBlock:
            {
                int span     = dragOrigEnd_ - dragOrigStart_;
                int newStart = juce::jlimit (r.lo, r.hi - span,
                                             dragOrigStart_ + (note - dragStartNote_));
                b.startNote = newStart;
                b.endNote   = newStart + span;
                break;
            }

            default: break;
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (editMode_ == EditMode::Idle) return;

        // Remove an invalid block (startNote > endNote should never happen, but guard anyway)
        if (editMode_ == EditMode::CreatingBlock
            && dragBlockIdx_ < (int) blocks_.size()
            && blocks_[dragBlockIdx_].startNote > blocks_[dragBlockIdx_].endNote)
        {
            blocks_.erase (blocks_.begin() + dragBlockIdx_);
        }

        editMode_     = EditMode::Idle;
        dragBlockIdx_ = -1;
        fireOnLayoutChanged();
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

private:
    // ── Constants ─────────────────────────────────────────────────────────────
    static constexpr int kKbdH     = 100;
    static constexpr int kPadAreaH = 80;
    static constexpr int kPadGap   = 4;

    // ── Edit state machine ────────────────────────────────────────────────────
    enum class EditMode { Idle, CreatingBlock, MovingBlock, ResizingStart, ResizingEnd };

    EditMode editMode_      = EditMode::Idle;
    int      dragBlockIdx_  = -1;   ///< Index into blocks_ being edited.
    int      dragAnchorNote_ = -1;  ///< Note at mouseDown for CreatingBlock anchor.
    int      dragOrigStart_ = -1;   ///< Original startNote for MovingBlock delta calc.
    int      dragOrigEnd_   = -1;   ///< Original endNote   for MovingBlock delta calc.
    int      dragStartNote_ = -1;   ///< Note at mouseDown  for MovingBlock delta calc.

    // ── JUCE components ───────────────────────────────────────────────────────
    juce::MidiKeyboardState      keyboardState_;
    juce::MidiKeyboardComponent  keyboard_;
    int                          rangeIndex_ = kDefaultRangeIndex;
    int                          numPads_    = 16;

    // ── Block data ────────────────────────────────────────────────────────────
    std::vector<KeyboardBlock>  blocks_;

    // ── Pad assignment data ───────────────────────────────────────────────────
    std::vector<PadAssignment>  padAssignments_;

    // ── Palette cycling for auto-assigned block colors ────────────────────────
    static juce::Colour colourForIndex (int idx) noexcept
    {
        static const ColourId palette[] = {
            ColourId::PaletteBlue,   ColourId::PaletteRed,    ColourId::PaletteGreen,
            ColourId::PaletteOrange, ColourId::PaletteViolet, ColourId::PaletteTeal
        };
        return ThemePalette::get (palette[((idx % 6) + 6) % 6]);
    }

    // ── Note / pixel conversion ───────────────────────────────────────────────

    /** @return MIDI note at @p relX (VirtualLayoutComponent coords). */
    int noteAtX (float relX) const noexcept
    {
        float x = relX - (float) keyboard_.getX();
        const auto& r = kRanges[rangeIndex_];
        for (int n = r.hi; n >= r.lo; --n)
            if (keyboard_.getKeyStartPosition (n) <= x + 0.5f)
                return n;
        return r.lo;
    }

    /** @return Right-edge x of @p note, relative to the keyboard's left edge. */
    float noteRightEdge (int note) const noexcept
    {
        const auto& r = kRanges[rangeIndex_];
        if (note + 1 <= r.hi)
            return keyboard_.getKeyStartPosition (note + 1);
        return (float) keyboard_.getWidth();
    }

    /** @return Pixel rect for @p b in VirtualLayoutComponent coordinates. */
    juce::Rectangle<float> blockRect (const KeyboardBlock& b) const noexcept
    {
        const auto& r = kRanges[rangeIndex_];
        int lo = juce::jlimit (r.lo, r.hi, b.startNote);
        int hi = juce::jlimit (r.lo, r.hi, b.endNote);
        float kx = (float) keyboard_.getX();
        float ky = (float) keyboard_.getY();
        float kh = (float) keyboard_.getHeight();
        float x1 = kx + keyboard_.getKeyStartPosition (lo);
        float x2 = kx + noteRightEdge (hi);
        return { x1, ky, x2 - x1, kh };
    }

    /** @brief Snap @p note to octave boundary (multiples of 12) when @p ctrlHeld. */
    static int snapNote (int note, bool ctrlHeld) noexcept
    {
        if (ctrlHeld)
            return juce::roundToInt ((float) note / 12.0f) * 12;
        return note;
    }

    // ── Rendering ─────────────────────────────────────────────────────────────

    static juce::String midiNoteName (int note)
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
    }

    juce::String blockLabel (const KeyboardBlock& b) const
    {
        juce::String range = midiNoteName (b.shiftedNote (b.startNote))
                           + "-"
                           + midiNoteName (b.shiftedNote (b.endNote));
        if (b.targetPluginPath.isNotEmpty())
        {
            juce::String name = juce::File (b.targetPluginPath).getFileNameWithoutExtension();
            return name + " (" + range + ")";
        }
        return range;
    }

    void paintBlockOverlays (juce::Graphics& g)
    {
        for (int i = 0; i < (int) blocks_.size(); ++i)
        {
            const auto& b  = blocks_[i];
            auto rect       = blockRect (b);
            if (rect.getWidth() < 1.0f) continue;

            // Semi-transparent fill — multiple overlapping blocks produce additive alpha,
            // making the layer region visually distinct.
            juce::Colour col = b.blockColour.withAlpha (0.45f);
            g.setColour (col);
            g.fillRect (rect);

            // Brighter border
            g.setColour (b.blockColour.brighter (0.5f).withAlpha (0.9f));
            g.drawRect (rect, 1.5f);

            // Selection highlight while dragging
            if (i == dragBlockIdx_ && editMode_ != EditMode::Idle)
            {
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRect (rect);
            }

            // Label: channel name + shifted note range (only if wide enough)
            if (rect.getWidth() >= 28.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.92f));
                g.setFont (juce::Font (10.0f, juce::Font::bold));
                g.drawText (blockLabel (b), rect.reduced (2.0f, 4.0f),
                            juce::Justification::centred, true);
            }
        }
    }

    void paintPadGrid (juce::Graphics& g)
    {
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
                int padIdx = r * cols + c;
                auto cell = juce::Rectangle<float> (
                    area.getX() + c * cellW + pad,
                    area.getY() + r * cellH + pad,
                    cellW - pad * 2.0f,
                    cellH - pad * 2.0f);

                // Look up assignment for this pad
                const PadAssignment* assignment = nullptr;
                for (const auto& pa : padAssignments_)
                    if (pa.padIndex == padIdx && pa.targetPluginPath.isNotEmpty())
                        { assignment = &pa; break; }

                g.setColour (assignment ? assignment->padColour.withAlpha (0.75f)
                                        : ThemePalette::get (ColourId::BgPanelAlt));
                g.fillRoundedRectangle (cell, 4.0f);

                g.setColour (assignment ? assignment->padColour.brighter (0.3f)
                                        : ThemePalette::get (ColourId::BorderDefault));
                g.drawRoundedRectangle (cell, 4.0f, 1.0f);

                // Pad number (top-left corner)
                g.setColour (ThemePalette::get (ColourId::TextSecondary));
                g.setFont (juce::Font (9.0f));
                g.drawText (juce::String (padIdx + 1),
                            cell.reduced (2.0f), juce::Justification::topLeft, false);

                // Bridge name (centre) if assigned
                if (assignment != nullptr)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.88f));
                    g.setFont (juce::Font (9.5f, juce::Font::bold));
                    juce::String name = juce::File (assignment->targetPluginPath)
                                            .getFileNameWithoutExtension();
                    g.drawText (name, cell.reduced (2.0f, 0.0f),
                                juce::Justification::centred, true);
                }
            }
        }
    }

    // ── Mouse cursor ──────────────────────────────────────────────────────────

    void updateCursorForPosition (juce::Point<float> pos)
    {
        if (! keyboard_.getBounds().toFloat().contains (pos))
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
            return;
        }
        static constexpr float kEdge = 7.0f;
        for (int i = (int) blocks_.size() - 1; i >= 0; --i)
        {
            auto r = blockRect (blocks_[i]);
            if (! r.contains (pos.x, pos.y)) continue;

            if (pos.x <= r.getX() + kEdge || pos.x >= r.getRight() - kEdge)
                setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            else
                setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            return;
        }
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    // ── Context menu ──────────────────────────────────────────────────────────

    void showBlockContextMenu (int blockIdx, juce::Point<int> screenPos)
    {
        const auto& block = blocks_[blockIdx];

        std::vector<std::pair<juce::String, juce::String>> bridges;
        if (getBridgeList) bridges = getBridgeList();

        // Bridge assignment sub-menu
        juce::PopupMenu bridgeMenu;
        bridgeMenu.addItem (1, "(None)", true, block.targetPluginPath.isEmpty());
        int id = 100;
        for (auto& [name, path] : bridges)
            bridgeMenu.addItem (id++, name, true, path == block.targetPluginPath);

        // Octave shift sub-menu (+3 at top → -3 at bottom)
        juce::PopupMenu octaveMenu;
        for (int ot = 3; ot >= -3; --ot)
        {
            juce::String lbl = ot == 0 ? "No shift"
                                       : (ot > 0 ? "+" : "") + juce::String (ot) + " oct";
            octaveMenu.addItem (200 + ot + 3, lbl, true, block.octaveShift == ot);
        }

        juce::PopupMenu menu;
        menu.addSubMenu ("Assign to...", bridgeMenu);
        menu.addSubMenu ("Octave shift", octaveMenu);
        menu.addSeparator();
        menu.addItem (300, "Delete block");

        menu.showMenuAsync (
            juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
            [this, blockIdx, bridges = std::move (bridges)] (int result) mutable
            {
                if (result == 0 || blockIdx >= (int) blocks_.size()) return;
                auto& b = blocks_[blockIdx];

                if (result == 1)                          // (None) — unassign
                {
                    b.targetPluginPath = {};
                    b.targetBridge     = nullptr;
                }
                else if (result >= 100 && result < 200)  // Bridge assignment
                {
                    int i = result - 100;
                    if (i < (int) bridges.size())
                    {
                        b.targetPluginPath = bridges[i].second;
                        b.targetBridge     = nullptr;  // resolved by MidiRoutingManager::setBlocks
                        b.blockColour      = colourForIndex (i);
                    }
                }
                else if (result >= 200 && result < 300)  // Octave shift
                {
                    b.octaveShift = (result - 200) - 3;
                }
                else if (result == 300)                   // Delete
                {
                    blocks_.erase (blocks_.begin() + blockIdx);
                }

                repaint();
                fireOnLayoutChanged();
            });
    }

    // ── Misc helpers ──────────────────────────────────────────────────────────

    void showPadContextMenu (int padIdx, juce::Point<int> screenPos)
    {
        std::vector<std::pair<juce::String, juce::String>> bridges;
        if (getBridgeList) bridges = getBridgeList();

        juce::String currentPath;
        for (const auto& pa : padAssignments_)
            if (pa.padIndex == padIdx) { currentPath = pa.targetPluginPath; break; }

        juce::PopupMenu menu;
        menu.addItem (1, "(None)", true, currentPath.isEmpty());
        int id = 100;
        for (auto& [name, path] : bridges)
            menu.addItem (id++, name, true, path == currentPath);

        menu.showMenuAsync (
            juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
            [this, padIdx, bridges = std::move (bridges)] (int result) mutable
            {
                if (result == 0) return;

                padAssignments_.erase (
                    std::remove_if (padAssignments_.begin(), padAssignments_.end(),
                        [padIdx] (const PadAssignment& pa) { return pa.padIndex == padIdx; }),
                    padAssignments_.end());

                if (result >= 100)
                {
                    int i = result - 100;
                    if (i < (int) bridges.size())
                    {
                        PadAssignment pa;
                        pa.padIndex         = padIdx;
                        pa.targetPluginPath = bridges[i].second;
                        pa.padColour        = colourForIndex (i);
                        pa.targetBridge     = nullptr;
                        padAssignments_.push_back (pa);
                    }
                }

                repaint();
                fireOnLayoutChanged();
            });
    }

    void fireOnLayoutChanged()
    {
        if (onLayoutChanged) onLayoutChanged (blocks_, padAssignments_);
    }

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualLayoutComponent)
};
