#pragma once
#include "UiCommon.h"
#include "UiComponents.h"
#include "BridgeInstance.h"

// Accent colour palette — cycles by channel index
static juce::Colour getMixerStripColor (int index)
{
    static const juce::Colour palette[] = {
        juce::Colour (0xff2a4a6a),   // steel blue
        juce::Colour (0xff6a2a2a),   // dark red
        juce::Colour (0xff2a6a2a),   // forest green
        juce::Colour (0xff6a4a2a),   // burnt orange
        juce::Colour (0xff4a2a6a),   // violet
        juce::Colour (0xff2a6a6a),   // teal
    };
    return palette[index % 6];
}

// ── MIDI parameter — used by MixerStrip and UIManager ───────────────────
enum class MixerParam { Fader, Pan, Mute, Solo };

// =====================================================================
// FXSlotComponent
// One FX slot in the Master strip: shows name, [B] bypass button.
// Click on name area → onToggleWindow callback.
// [B] button        → toggles BridgeInstance::mixerBypassed.
// =====================================================================
class FXSlotComponent : public Component
{
public:
    BridgeInstance*       bridge       = nullptr;
    BridgeInstance*       parentBridge = nullptr;  // instrument context (nullptr = master chain)
    std::function<void()> onToggleWindow;
    std::function<void(BridgeInstance*)> onAddFx;  // fired when placeholder (+) is clicked

    // Set true while this slot is being dragged (dims the appearance)
    bool isDragging = false;

    FXSlotComponent()
    {
        bypassBtn.setButtonText ("B");
        bypassBtn.setClickingTogglesState (true);
        bypassBtn.setColour (TextButton::buttonColourId,   juce::Colour (0xff2a2a38));
        bypassBtn.setColour (TextButton::buttonOnColourId, juce::Colour (0xffcc3333));
        bypassBtn.setTooltip ("Bypass FX");
        bypassBtn.onClick = [this] {
            if (bridge != nullptr)
                bridge->mixerBypassed.store (bypassBtn.getToggleState(),
                                             std::memory_order_relaxed);
            repaint();
        };
        addAndMakeVisible (bypassBtn);
    }

    void setFxName (const juce::String& name) { nameStr = name; repaint(); }

    void setBypassed (bool b)
    {
        bypassBtn.setToggleState (b, dontSendNotification);
        repaint();
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (bridge == nullptr)
        {
            if (onAddFx) onAddFx (parentBridge);  // pass instrument context (nullptr = master chain)
            return;
        }
        if (bridge->getState() != BridgeInstance::State::Connected)
            return;
        dragStartPos_ = e.getPosition();
        dragStarted_  = false;
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (bridge == nullptr) return;
        if (! dragStarted_ && e.getDistanceFromDragStart() > 4)
        {
            dragStarted_ = true;
            if (auto* container = DragAndDropContainer::findParentDragContainerFor (this))
            {
                isDragging = true;
                repaint();
                container->startDragging ("FXSlot", this);
            }
        }
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (bridge == nullptr) return;
        if (! dragStarted_)
        {
            // Short click — toggle window visibility
            if (bridge->getState() != BridgeInstance::State::Connected) return;
            windowShown_ = ! windowShown_;
            if (onToggleWindow) onToggleWindow();
            repaint();
        }
        isDragging  = false;
        dragStarted_ = false;
        repaint();
    }

    void paint (Graphics& g) override
    {
        auto bounds = getLocalBounds().reduced (1);
        bool isPlaceholder = (bridge == nullptr);
        bool bypassed = bypassBtn.getToggleState();

        float alpha = isDragging ? 0.35f : 1.0f;

        g.setColour ((isPlaceholder ? juce::Colour (0xff1a1a25)
                    : bypassed      ? juce::Colour (0xff1e1414)
                                    : juce::Colour (0xff1e1e30)).withAlpha (alpha));
        g.fillRoundedRectangle (bounds.toFloat(), 2.0f);
        g.setColour (juce::Colour (0xff333344).withAlpha (alpha));
        g.drawRoundedRectangle (bounds.toFloat(), 2.0f, 1.0f);

        auto textArea = bounds.withTrimmedRight (isPlaceholder ? 4 : 22).reduced (3, 0);
        g.setColour ((isPlaceholder ? juce::Colour (0xff444455)
                    : bypassed      ? juce::Colour (0xff555566)
                    : windowShown_  ? juce::Colours::white.withAlpha (0.85f)
                                    : juce::Colour (0xffaaaacc)).withAlpha (alpha));
        g.setFont (Font (9.5f));
        g.drawText (nameStr, textArea,
                    isPlaceholder ? Justification::centred : Justification::centredLeft, true);
    }

    void resized() override
    {
        if (bridge != nullptr)
            bypassBtn.setBounds (getLocalBounds().removeFromRight (20).reduced (1));
        else
            bypassBtn.setBounds ({});  // hide bypass button for placeholders
    }

private:
    juce::String nameStr;
    TextButton   bypassBtn;
    bool         windowShown_ = true;
    bool         dragStarted_ = false;
    juce::Point<int> dragStartPos_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FXSlotComponent)
};

// =====================================================================
// MixerStrip
// =====================================================================
class MixerStrip : public Component
{
public:
    static constexpr int kFxSlotH       = 17;  // height per FX slot row (px)
    static constexpr int kFxVisibleRows  = 3;   // always-visible rows (fixed area height)
    static constexpr int kFxAreaH        = kFxVisibleRows * kFxSlotH;  // 85 px

    // Callbacks — wired by MixerContentComponent after construction
    std::function<void(float)>               onFaderChange;      // linear gain 0.0–1.5 (ch) / 0.0–1.0 (master)
    std::function<void(float)>               onPanChange;        // -1.0 to +1.0
    std::function<void(bool)>                onMuteChange;
    std::function<void(bool)>                onSoloChange;
    std::function<void(const juce::String&)> onNameChange;       // fired when user edits channel name
    std::function<void(juce::Colour)>        onColorChange;      // fired when user picks accent colour
    std::function<void(BridgeInstance*)>          onAddFx;            // bubbled up from placeholder FXSlotComponents
    std::function<void(MixerParam)>               onMidiLearnRequest; // right-click → MIDI Learn
    std::function<void(MixerParam)>               onMidiClearMapping; // right-click → Clear Mapping
    std::function<void(BridgeInstance*, int)>     onFxReorderRequest; // drag-drop reorder within strip

    ~MixerStrip() override { fader.setLookAndFeel (nullptr); }

    MixerStrip (const juce::String& displayName,
                juce::Colour        stripColor,
                bool                isMaster = false)
        : accentColor (stripColor), isMasterStrip (isMaster)
    {
        nameLabel.setText (displayName, dontSendNotification);
        nameLabel.setJustificationType (Justification::centred);
        nameLabel.setFont (Font (12.0f, Font::bold));
        nameLabel.setEditable (false, true, false);  // double-click to edit
        nameLabel.setColour (Label::backgroundColourId, accentColor.withAlpha (0.35f));
        nameLabel.setColour (Label::textColourId, juce::Colours::white);
        nameLabel.onEditorHide = [this] {
            if (onNameChange) onNameChange (nameLabel.getText());
        };
        nameLabel.addMouseListener (this, false);  // catch right-click for colour picker
        addAndMakeVisible (nameLabel);

        addAndMakeVisible (ledMeter);

        fxViewport_.setScrollBarsShown (true, false);  // vertical scroll only
        fxViewport_.setViewedComponent (&fxContent_, false);
        addAndMakeVisible (fxViewport_);

        panSlider.setSliderStyle (Slider::LinearHorizontal);
        panSlider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        panSlider.setRange (-1.0, 1.0);
        panSlider.setValue (0.0, dontSendNotification);
        panSlider.setTooltip ("Pan");
        panSlider.onValueChange = [this] {
            float v = (float) panSlider.getValue();
            panValueLabel.setText (v == 0.f ? "C" : (v > 0.f ? "R" + juce::String (v, 2)
                                                              : "L" + juce::String (-v, 2)),
                                   dontSendNotification);
            if (onPanChange) onPanChange (v);
        };
        addAndMakeVisible (panSlider);

        panValueLabel.setText ("C", dontSendNotification);
        panValueLabel.setJustificationType (Justification::centred);
        panValueLabel.setFont (Font (9.0f));
        panValueLabel.setColour (Label::textColourId, juce::Colour (0xff888899));
        addAndMakeVisible (panValueLabel);

        fader.setSliderStyle (Slider::LinearVertical);
        fader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        fader.setRange (0.0, isMaster ? 1.0 : 1.5);
        fader.setValue (1.0, dontSendNotification);
        fader.setTooltip ("Volume");
        fader.onValueChange = [this] {
            float v = (float) fader.getValue();
            faderValueLabel.setText (juce::String (v, 2), dontSendNotification);
            if (onFaderChange) onFaderChange (v);
        };
        fader.setLookAndFeel (&faderLF);
        addAndMakeVisible (fader);

        faderValueLabel.setText ("1.00", dontSendNotification);
        faderValueLabel.setJustificationType (Justification::centred);
        faderValueLabel.setFont (Font (9.0f));
        faderValueLabel.setColour (Label::textColourId, juce::Colour (0xff888899));
        addAndMakeVisible (faderValueLabel);

        muteBtn.setButtonText ("M");
        muteBtn.setClickingTogglesState (true);
        muteBtn.setColour (TextButton::buttonColourId,   juce::Colour (0xff333344));
        muteBtn.setColour (TextButton::buttonOnColourId, juce::Colours::red.withAlpha (0.7f));
        muteBtn.setTooltip ("Mute");
        muteBtn.onClick = [this] {
            if (onMuteChange) onMuteChange (muteBtn.getToggleState());
        };
        addAndMakeVisible (muteBtn);

        soloBtn.setButtonText ("S");
        soloBtn.setClickingTogglesState (true);
        soloBtn.setColour (TextButton::buttonColourId,   juce::Colour (0xff333344));
        soloBtn.setColour (TextButton::buttonOnColourId, juce::Colours::yellow.withAlpha (0.7f));
        soloBtn.setTooltip ("Solo");
        soloBtn.onClick = [this] {
            if (onSoloChange) onSoloChange (soloBtn.getToggleState());
        };
        addAndMakeVisible (soloBtn);

        // Register as mouse listener on sub-controls for right-click MIDI menu
        fader    .addMouseListener (this, false);
        panSlider.addMouseListener (this, false);
        muteBtn  .addMouseListener (this, false);
        soloBtn  .addMouseListener (this, false);
    }

    void setMeterVisible (bool v) { ledMeter.setVisible (v); resized(); }
    void setDisplayName (const juce::String& name) { nameLabel.setText (name, dontSendNotification); }

    // FX slot management
    FXSlotComponent* addFxSlot()
    {
        auto* slot = fxContent_.slots.add (new FXSlotComponent());
        fxContent_.addAndMakeVisible (slot);
        // Keep FxContent's reorder callback in sync with MixerStrip's callback
        fxContent_.onFxReorderRequest = [this] (BridgeInstance* b, int newIdx) {
            if (onFxReorderRequest) onFxReorderRequest (b, newIdx);
        };
        resized();
        return slot;
    }

    void clearFxSlots()
    {
        fxContent_.slots.clear();
        resized();
    }

    // Initialise UI controls from saved values without triggering callbacks
    void setInitialValues (float gain, float pan, bool muted)
    {
        fader.setValue     (gain, dontSendNotification);
        panSlider.setValue (pan,  dontSendNotification);
        muteBtn.setToggleState (muted, dontSendNotification);

        faderValueLabel.setText (juce::String (gain, 2), dontSendNotification);
        panValueLabel.setText (pan == 0.f ? "C" : (pan > 0.f ? "R" + juce::String (pan, 2)
                                                              : "L" + juce::String (-pan, 2)),
                               dontSendNotification);
    }

    // Set fader position without triggering onFaderChange (used for sync)
    void setFaderNoCallback (float v)
    {
        fader.setValue ((double) v, dontSendNotification);
        faderValueLabel.setText (juce::String (v, 2), dontSendNotification);
    }

    // Set pan/mute/solo without triggering callbacks (used for MIDI remote sync)
    void setPanNoCallback (float v)
    {
        panSlider.setValue (v, dontSendNotification);
        panValueLabel.setText (v == 0.f ? "C" : (v > 0.f ? "R" + juce::String (v, 2)
                                                           : "L" + juce::String (-v, 2)),
                               dontSendNotification);
    }
    void setMuteNoCallback (bool muted)  { muteBtn.setToggleState (muted,  dontSendNotification); }
    void setSoloNoCallback (bool soloed) { soloBtn.setToggleState (soloed, dontSendNotification); }

    // Enter / exit MIDI Learn mode for a specific parameter (highlights the control)
    void setLearnMode (MixerParam p, bool active)
    {
        learnParam_  = p;
        learnActive_ = active;
        repaint();
    }

    // Change accent colour programmatically (e.g. on project load)
    void setAccentColor (juce::Colour c)
    {
        accentColor = c;
        nameLabel.setColour (Label::backgroundColourId, accentColor.withAlpha (0.35f));
        repaint();
    }

    // Mark this strip as belonging to the Global Layer (Stage Set Slot 0).
    // Draws a thin gold bar at the top and tints the border to distinguish it visually.
    void setIsGlobal (bool g) { isGlobal_ = g; repaint(); }

    // Called by the UI timer — passes post-fader peak values (0.0 – 1.0+)
    void updateMeter (float l, float r) { ledMeter.setLevels (l, r); }

    // Visually dim this strip when another channel is soloed
    void setSoloDimmed (bool dimmed) { setAlpha (dimmed ? 0.38f : 1.0f); }

    bool isSoloed() const { return soloBtn.getToggleState(); }

    // Force solo button state without firing onSoloChange (used by exclusive-solo logic)
    void setSoloActive (bool active) { soloBtn.setToggleState (active, dontSendNotification); }

    void mouseDown (const MouseEvent& e) override
    {
        if (! e.mods.isRightButtonDown()) return;

        auto* src = e.eventComponent;
        if      (src == &fader)     showMidiMenu (MixerParam::Fader);
        else if (src == &panSlider) showMidiMenu (MixerParam::Pan);
        else if (src == &muteBtn)   showMidiMenu (MixerParam::Mute);
        else if (src == &soloBtn)   showMidiMenu (MixerParam::Solo);
        else                        showColorMenu();
    }

    void paint (Graphics& g) override
    {
        auto bounds = getLocalBounds().reduced (2);
        g.setColour (juce::Colour (0xff20202a));
        g.fillRoundedRectangle (bounds.toFloat(), 4.0f);

        // Global strips get a gold border instead of the default dark outline.
        g.setColour (isGlobal_ ? juce::Colour (0xffb8860b) : juce::Colour (0xff333344));
        g.drawRoundedRectangle (bounds.toFloat(), 4.0f, isGlobal_ ? 1.5f : 1.0f);

        g.setColour (accentColor.withAlpha (0.7f));
        g.fillRect (getLocalBounds().reduced (2).removeFromBottom (3));

        // Global Layer indicator: thin gold bar at the top of the strip.
        if (isGlobal_)
        {
            g.setColour (juce::Colour (0xffb8860b));
            g.fillRect (getLocalBounds().reduced (2).removeFromTop (3));
        }

        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.fillRect (fader.getBounds().reduced (8, 0));
    }

    // Draw fader tick marks on top of the slider
    void paintOverChildren (Graphics& g) override
    {
        auto fb = fader.getBounds();
        if (fb.isEmpty()) return;

        // Must match FaderLookAndFeel::getSliderThumbRadius() = 5
        const float thumbInset = 5.0f;
        float trackTop    = (float) fb.getY()      + thumbInset;
        float trackBottom = (float) fb.getBottom() - thumbInset;
        float trackH = trackBottom - trackTop;
        if (trackH <= 0.0f) return;

        double maxVal = fader.getMaximum();
        auto valueToY = [&] (double v) -> float
        {
            return trackBottom - (float) (v / maxVal * trackH);
        };

        auto isMajor = [] (double v) -> bool
        {
            double rem = v - std::floor (v / 0.5 + 0.5) * 0.5;
            return std::abs (rem) < 0.001;
        };

        // Minor ticks every 0.05 (left-edge short lines only)
        int numSteps = (int) std::round (maxVal / 0.05);
        for (int i = 0; i <= numSteps; ++i)
        {
            double v = i * 0.05;
            if (v > maxVal + 0.001) break;
            if (isMajor (v)) continue;
            g.setColour (juce::Colour (0x88aaaaaa));
            float y = valueToY (v);
            g.drawHorizontalLine ((int) y, (float) fb.getX(), (float) (fb.getX() + 7));
        }

        // Major ticks — full width lines
        // 0.0
        g.setColour (juce::Colour (0xaa9999aa));
        g.drawHorizontalLine ((int) valueToY (0.0), (float) fb.getX(), (float) fb.getRight());

        // 0.5
        g.setColour (juce::Colour (0xbbaaaabb));
        g.drawHorizontalLine ((int) valueToY (0.5), (float) fb.getX(), (float) fb.getRight());

        // 1.0 (0 dB / unity) — most prominent
        g.setColour (juce::Colour (0xddccccdd));
        g.drawHorizontalLine ((int) valueToY (1.0), (float) fb.getX(), (float) fb.getRight());

        // MAX (1.5) — channels only, red tint
        if (! isMasterStrip)
        {
            g.setColour (juce::Colour (0xbbdd6666));
            g.drawHorizontalLine ((int) valueToY (1.5), (float) fb.getX(), (float) fb.getRight());
        }

        // MIDI Learn highlight — yellow border around the learning control
        if (learnActive_)
        {
            Component* target = nullptr;
            switch (learnParam_)
            {
                case MixerParam::Fader: target = &fader;     break;
                case MixerParam::Pan:   target = &panSlider;  break;
                case MixerParam::Mute:  target = &muteBtn;    break;
                case MixerParam::Solo:  target = &soloBtn;    break;
            }
            if (target != nullptr)
            {
                g.setColour (juce::Colours::yellow.withAlpha (0.75f));
                g.drawRect (target->getBounds().expanded (2), 2);
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6);

        if (ledMeter.isVisible())
            ledMeter.setBounds (area.removeFromTop (28).reduced (10, 2));

        nameLabel.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);

        // Scrollable FX area — always kFxAreaH tall regardless of slot count
        fxViewport_.setBounds (area.removeFromTop (kFxAreaH).reduced (0, 1));
        {
            int contentW = juce::jmax (1, fxViewport_.getMaximumVisibleWidth());
            int contentH = juce::jmax (kFxAreaH, fxContent_.slots.size() * kFxSlotH);
            fxContent_.setSize (contentW, contentH);
            fxContent_.resized();  // force slot re-layout (setSize skips resized() when size unchanged)
        }
        area.removeFromTop (4);

        panSlider.setBounds (area.removeFromTop (16).reduced (8, 0));
        panValueLabel.setBounds (area.removeFromTop (11));
        area.removeFromTop (2);

        auto footer = area.removeFromBottom (24);
        muteBtn.setBounds (footer.removeFromLeft (footer.getWidth() / 2).reduced (2));
        soloBtn.setBounds (footer.reduced (2));

        faderValueLabel.setBounds (area.removeFromBottom (12));
        {
            auto fb = area.reduced (4, 0);
            int w = jmax (16, fb.getWidth() / 3);
            fader.setBounds (fb.withSizeKeepingCentre (w, fb.getHeight()));
        }
    }

private:
    // Custom LookAndFeel: rectangular thumb for the vertical fader
    struct FaderLookAndFeel : public juce::LookAndFeel_V4
    {
        // thumbH = 9 → half = 4.5 → radius = 5
        // JUCE uses this to compute sliderPos range: [y+5, y+height-5]
        // paintOverChildren must use the same thumbInset (5.0f)
        int getSliderThumbRadius (Slider&) override { return 5; }

        void drawLinearSlider (Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               Slider::SliderStyle style, Slider& slider) override
        {
            if (style != Slider::LinearVertical)
            {
                LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                  minSliderPos, maxSliderPos, style, slider);
                return;
            }

            // Track (within thumb travel range)
            const float thumbRadius = 5.0f;
            const float trackW = 4.0f;
            const float cx = (float) x + (float) width * 0.5f;
            g.setColour (slider.findColour (Slider::trackColourId));
            g.fillRect (cx - trackW * 0.5f, (float) y + thumbRadius,
                        trackW, (float) height - 2.0f * thumbRadius);

            // Rectangular thumb — sliderPos is the thumb CENTER y
            const float thumbW = (float) width * 0.85f;
            const float thumbH = 9.0f;
            const float ty = sliderPos - thumbH * 0.5f;
            auto base = slider.isEnabled() ? slider.findColour (Slider::thumbColourId)
                                           : juce::Colours::grey;
            g.setColour (base);
            g.fillRect (cx - thumbW * 0.5f, ty, thumbW, thumbH);
            g.setColour (base.brighter (0.6f));
            g.drawHorizontalLine ((int) ty, cx - thumbW * 0.5f, cx + thumbW * 0.5f);
            g.setColour (base.darker (0.4f));
            g.drawHorizontalLine ((int) (ty + thumbH - 1.0f), cx - thumbW * 0.5f, cx + thumbW * 0.5f);
        }

        void drawLinearSliderThumb (Graphics& g, int x, int y, int width, int height,
                                    float sliderPos, float minSliderPos, float maxSliderPos,
                                    Slider::SliderStyle style, Slider& slider) override
        {
            LookAndFeel_V4::drawLinearSliderThumb (g, x, y, width, height, sliderPos,
                                                   minSliderPos, maxSliderPos, style, slider);
        }
    };

    // Real LED meter — 12 segments, L and R bars
    struct RealLedMeter : public Component
    {
        void setLevels (float l, float r) { levelL = l; levelR = r; repaint(); }

        void paint (Graphics& g) override
        {
            auto area = getLocalBounds();
            int half = area.getWidth() / 2 - 1;
            drawBar (g, area.removeFromLeft (half), levelL);
            area.removeFromLeft (2);
            drawBar (g, area, levelR);
        }

    private:
        void drawBar (Graphics& g, Rectangle<int> area, float level)
        {
            const int N    = 12;
            const int segH = jmax (1, (area.getHeight() - (N - 1)) / N);
            int filled = jmin (N, (int) (level * N + 0.5f));

            for (int i = 0; i < N; ++i)
            {
                int y = area.getBottom() - (i + 1) * segH - i;
                Rectangle<int> seg (area.getX(), y, area.getWidth(), segH);

                juce::Colour c;
                if      (i >= N - 2) c = juce::Colour (0xffff2222);   // top 2:  red
                else if (i >= N - 5) c = juce::Colour (0xffffcc00);   // next 3: yellow
                else                 c = juce::Colour (0xff00dd44);   // rest:   green

                g.setColour (i < filled ? c : c.withAlpha (0.1f));
                g.fillRect (seg);
            }
        }

        float levelL = 0.f, levelR = 0.f;
    };

    void showColorMenu()
    {
        static const struct { const char* name; juce::Colour color; } kPalette[] = {
            { "Steel Blue",   juce::Colour (0xff2a4a6a) },
            { "Dark Red",     juce::Colour (0xff6a2a2a) },
            { "Forest Green", juce::Colour (0xff2a6a2a) },
            { "Burnt Orange", juce::Colour (0xff6a4a2a) },
            { "Violet",       juce::Colour (0xff4a2a6a) },
            { "Teal",         juce::Colour (0xff2a6a6a) },
        };
        PopupMenu m;
        for (int i = 0; i < 6; ++i)
            m.addItem (i + 1, kPalette[i].name, true, accentColor == kPalette[i].color);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this),
            [this] (int r)
            {
                static const juce::Colour kColors[] = {
                    juce::Colour (0xff2a4a6a), juce::Colour (0xff6a2a2a),
                    juce::Colour (0xff2a6a2a), juce::Colour (0xff6a4a2a),
                    juce::Colour (0xff4a2a6a), juce::Colour (0xff2a6a6a),
                };
                if (r >= 1 && r <= 6)
                {
                    accentColor = kColors[r - 1];
                    nameLabel.setColour (Label::backgroundColourId, accentColor.withAlpha (0.35f));
                    repaint();
                    if (onColorChange) onColorChange (accentColor);
                }
            });
    }

    void showMidiMenu (MixerParam param)
    {
        PopupMenu m;
        m.addItem (1, LvhStr ("STR_MIDI_LEARN"));
        m.addItem (2, LvhStr ("STR_MIDI_CLEAR_MAP"));
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this),
            [this, param] (int r)
            {
                if (r == 1 && onMidiLearnRequest) onMidiLearnRequest (param);
                if (r == 2 && onMidiClearMapping) onMidiClearMapping (param);
            });
    }

    // Scrollable FX container — also acts as the D&D container and drop target
    struct FxContent : public juce::Component,
                       public juce::DragAndDropContainer,
                       public juce::DragAndDropTarget
    {
        juce::OwnedArray<FXSlotComponent> slots;

        // Fired when the user drops a slot at a new position.
        // Arguments: (bridge being moved, new slot index within this strip's FX list)
        std::function<void(BridgeInstance*, int)> onFxReorderRequest;

        void resized() override
        {
            int y = 0;
            for (auto* s : slots)
            {
                s->setBounds (0, y, getWidth(), MixerStrip::kFxSlotH - 2);
                y += MixerStrip::kFxSlotH;
            }
        }

        // ── DragAndDropTarget ─────────────────────────────────────────────
        bool isInterestedInDragSource (const SourceDetails& details) override
        {
            // Only accept FXSlot drags from slots in this same FxContent
            if (details.description.toString() != "FXSlot") return false;
            auto* slot = dynamic_cast<FXSlotComponent*> (details.sourceComponent.get());
            return slot != nullptr && slot->bridge != nullptr && slots.contains (slot);
        }

        void itemDragMove (const SourceDetails& details) override
        {
            dropLineY_ = calcDropIndex (details.localPosition.y) * MixerStrip::kFxSlotH;
            showDropLine_ = true;
            repaint();
        }

        void itemDragExit (const SourceDetails&) override
        {
            showDropLine_ = false;
            repaint();
        }

        void itemDropped (const SourceDetails& details) override
        {
            showDropLine_ = false;
            repaint();

            auto* slot = dynamic_cast<FXSlotComponent*> (details.sourceComponent.get());
            if (slot == nullptr || slot->bridge == nullptr) return;

            slot->isDragging = false;
            slot->repaint();

            int newIdx = calcDropIndex (details.localPosition.y);
            if (onFxReorderRequest)
                onFxReorderRequest (slot->bridge, newIdx);
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (! showDropLine_) return;
            g.setColour (juce::Colour (0xff88aaff));
            g.fillRect (0, dropLineY_ - 1, getWidth(), 2);
        }

    private:
        bool showDropLine_ = false;
        int  dropLineY_    = 0;

        // Returns the slot index (0-based) at which to insert, ignoring placeholder
        int calcDropIndex (int localY) const
        {
            // Count non-placeholder slots
            int fxCount = 0;
            for (auto* s : slots)
                if (s->bridge != nullptr) ++fxCount;

            int idx = juce::jlimit (0, fxCount, localY / MixerStrip::kFxSlotH);
            return idx;
        }
    };

    FaderLookAndFeel         faderLF;      // must be declared before fader
    juce::Colour             accentColor;
    bool                     isMasterStrip  = false;
    bool                     isGlobal_      = false;
    MixerParam               learnParam_    = MixerParam::Fader;
    bool                     learnActive_   = false;
    Label                    nameLabel;
    RealLedMeter             ledMeter;
    FxContent                fxContent_;   // must be declared before fxViewport_
    juce::Viewport           fxViewport_;
    Slider                   panSlider, fader;
    Label                    panValueLabel, faderValueLabel;
    TextButton               muteBtn, soloBtn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrip)
};

// =====================================================================
// MixerContentComponent
// =====================================================================
class MixerContentComponent : public Component,
                              private juce::Timer
{
public:
    std::function<void(bool)> onPinToggled;

    MixerContentComponent()
    {
        meterBtn = std::make_unique<IconButton> ("Toggle LED Meters", Icons::led);
        meterBtn->setClickingTogglesState (true);
        meterBtn->setToggleState (true, dontSendNotification);
        meterBtn->setColour (TextButton::buttonColourId,   juce::Colour (0xff333344));
        meterBtn->setColour (TextButton::buttonOnColourId, juce::Colour (0xff2a4a3a));
        meterBtn->onClick = [this] { applyMeterVisibility(); };
        addAndMakeVisible (*meterBtn);

        pinBtn_ = std::make_unique<IconButton> ("Always on Top", Icons::pin);
        pinBtn_->setClickingTogglesState (true);
        pinBtn_->setColour (TextButton::buttonColourId,   juce::Colour (0xff252535));
        pinBtn_->setColour (TextButton::buttonOnColourId, juce::Colour (0xffaa6600));
        pinBtn_->setTooltip ("Pin window on top");
        pinBtn_->onClick = [this] {
            if (onPinToggled) onPinToggled (pinBtn_->getToggleState());
        };
        addAndMakeVisible (*pinBtn_);

        inputStrip_ = std::make_unique<MixerStrip> ("INPUT", juce::Colour (0xff1a4a3a));
        inputStrip_->setMeterVisible (true);
        inputStrip_->onFaderChange = [this] (float v) {
            if (onInputGainChange) onInputGainChange (v);
        };
        inputStrip_->onMuteChange = [this] (bool muted) {
            if (onInputMuteChange) onInputMuteChange (muted);
        };
        addAndMakeVisible (*inputStrip_);

        masterStrip = std::make_unique<MixerStrip> ("MASTER", juce::Colour (0xff444455), true);
        addAndMakeVisible (*masterStrip);
        masterStrip->onFaderChange = [this] (float v) {
            if (onMasterGainChange) onMasterGainChange (v);
        };
        // nullptr = master (no BridgeInstance)
        masterStrip->onMidiLearnRequest = [this] (MixerParam p) {
            if (onMidiLearnRequest) onMidiLearnRequest (nullptr, p);
        };
        masterStrip->onMidiClearMapping = [this] (MixerParam p) {
            if (onMidiClearMapping) onMidiClearMapping (nullptr, p);
        };

        startTimer (kMeterIntervalMs);  // start meter refresh timer
    }

    ~MixerContentComponent() override { stopTimer(); }

    // Called when Core volume slider changes — updates MASTER fader position without callback loop
    void setMasterGain (float v) { masterStrip->setFaderNoCallback (v); }

    std::function<void(float)>                        onMasterGainChange;
    std::function<void(BridgeInstance*)>              onToggleFxWindow;
    std::function<void(BridgeInstance*)>              onAddFx;              // bubbled up from any placeholder (+) slot
    std::function<void(BridgeInstance*, MixerParam)>  onMidiLearnRequest;   // bubbled up from strip right-click
    std::function<void(BridgeInstance*, MixerParam)>  onMidiClearMapping;   // bubbled up from strip right-click
    std::function<void(BridgeInstance*, int)>         onFxReorderRequest;   // bubbled up from FX D&D reorder

    // Physical input strip callbacks
    std::function<void(float)>                        onInputGainChange;
    std::function<void(bool)>                         onInputMuteChange;
    std::function<std::pair<float,float>()>           getInputPeaks;

    // Called by UIManager to highlight/remove learn indicator on a strip.
    // b == nullptr targets the Master strip.
    void setStripLearnMode (BridgeInstance* b, MixerParam p, bool active)
    {
        if (b == nullptr) { masterStrip->setLearnMode (p, active); return; }
        int idx = bridges_.indexOf (b);
        if (isPositiveAndBelow (idx, strips.size()))
            strips[idx]->setLearnMode (p, active);
    }

    // Called by UIManager to sync a control's UI after a MIDI CC update.
    // b == nullptr targets the Master strip (fader only).
    void applyMidiValue (BridgeInstance* b, MixerParam p, float value)
    {
        if (b == nullptr)
        {
            if (p == MixerParam::Fader) masterStrip->setFaderNoCallback (value);
            if (p == MixerParam::Pan)   masterStrip->setPanNoCallback   (value);
            return;
        }
        int idx = bridges_.indexOf (b);
        if (! isPositiveAndBelow (idx, strips.size())) return;
        auto* s = strips[idx];
        switch (p)
        {
            case MixerParam::Fader: s->setFaderNoCallback (value);           break;
            case MixerParam::Pan:   s->setPanNoCallback   (value);           break;
            case MixerParam::Mute:  s->setMuteNoCallback  (value > 0.5f);   break;
            case MixerParam::Solo:
                s->setSoloNoCallback (value > 0.5f);
                updateSoloDimming();
                break;
        }
    }

    // Rebuild channel strips and FX slots to match the supplied bridge lists.
    void updateBridges (const juce::Array<BridgeInstance*>& instrumentBridges,
                        const juce::Array<BridgeInstance*>& effectBridges = {})
    {
        bridges_.clear();
        strips.clear();
        bool metersOn = meterBtn->getToggleState();

        for (int i = 0; i < instrumentBridges.size(); ++i)
        {
            auto* b = instrumentBridges[i];
            bridges_.add (b);

            juce::String name = b->mixerCustomName.isNotEmpty()
                                ? b->mixerCustomName
                                : juce::File (b->getPluginPath()).getFileNameWithoutExtension();
            juce::Colour color = b->mixerCustomColor.getAlpha() > 0
                                 ? b->mixerCustomColor
                                 : getMixerStripColor (i);
            auto* strip = strips.add (new MixerStrip (name, color));
            strip->setIsGlobal (b->isGlobal());
            strip->setMeterVisible (metersOn);
            strip->setInitialValues (b->mixerGain.load(),
                                     b->mixerPan.load(),
                                     b->mixerMuted.load());

            // Wire fader/pan/mute/solo callbacks → BridgeInstance atomics
            strip->onFaderChange = [b] (float v) {
                b->mixerGain.store (v, std::memory_order_relaxed);
            };
            strip->onPanChange = [b] (float v) {
                b->mixerPan.store (v, std::memory_order_relaxed);
            };
            strip->onMuteChange = [b] (bool muted) {
                b->mixerMuted.store (muted, std::memory_order_relaxed);
            };
            strip->onSoloChange = [this, b] (bool soloed) {
                b->mixerSoloed.store (soloed, std::memory_order_relaxed);
                updateSoloDimming();
            };
            strip->onNameChange = [b] (const juce::String& newName) {
                b->mixerCustomName = newName;
                // Reflect the new strip label in the Bridge window title
                juce::String title = (b->getRole() == BridgeInstance::Role::Effect)
                                     ? "LVH-Bridge [FX]: [" + newName + "]"
                                     : "LVH-Bridge [" + newName + "]";
                b->sendWindowTitle (title);
            };
            strip->onColorChange = [b] (juce::Colour c) {
                b->mixerCustomColor = c;
            };
            strip->onAddFx = [this] (BridgeInstance* parent) {
                if (onAddFx) onAddFx (parent);
            };
            strip->onMidiLearnRequest = [this, b] (MixerParam p) {
                if (onMidiLearnRequest) onMidiLearnRequest (b, p);
            };
            strip->onMidiClearMapping = [this, b] (MixerParam p) {
                if (onMidiClearMapping) onMidiClearMapping (b, p);
            };
            strip->onFxReorderRequest = [this] (BridgeInstance* fx, int newIdx) {
                if (onFxReorderRequest) onFxReorderRequest (fx, newIdx);
            };

            // Per-channel FX slots for this instrument strip
            strip->clearFxSlots();
            for (auto* fx : effectBridges)
            {
                if (fx->getFxParentPath().isEmpty()) continue;
                if (juce::File (fx->getFxParentPath()) != juce::File (b->getPluginPath())) continue;

                juce::String fxName = fx->mixerCustomName.isNotEmpty()
                                      ? fx->mixerCustomName
                                      : juce::File (fx->getPluginPath()).getFileNameWithoutExtension();
                auto* slot = strip->addFxSlot();
                slot->bridge = fx;
                slot->setFxName (fxName);
                slot->setBypassed (fx->mixerBypassed.load());
                slot->onToggleWindow = [this, fx] {
                    if (onToggleFxWindow) onToggleFxWindow (fx);
                };
            }
            // One + placeholder for adding per-channel FX to this instrument
            {
                auto* placeholder = strip->addFxSlot();
                placeholder->setFxName ("+");
                placeholder->parentBridge = b;
                placeholder->onAddFx = [this] (BridgeInstance* parent) {
                    if (onAddFx) onAddFx (parent);
                };
            }

            addAndMakeVisible (strip);
        }

        // Rebuild FX slots in the Master strip from master effects (fxParentPath empty)
        masterStrip->clearFxSlots();
        for (auto* b : effectBridges)
        {
            if (b->getFxParentPath().isNotEmpty()) continue;  // skip per-channel FX
            juce::String name = b->mixerCustomName.isNotEmpty()
                                ? b->mixerCustomName
                                : juce::File (b->getPluginPath()).getFileNameWithoutExtension();
            auto* slot = masterStrip->addFxSlot();
            slot->bridge = b;
            slot->resized();  // refresh bypass button visibility now that bridge is set
            slot->setFxName (name);
            slot->setBypassed (b->mixerBypassed.load());
            slot->onToggleWindow = [this, b] {
                if (onToggleFxWindow) onToggleFxWindow (b);
            };
        }

        // One + placeholder for adding the next master FX
        {
            auto* placeholder = masterStrip->addFxSlot();
            placeholder->setFxName ("+");
            placeholder->onAddFx = [this] (BridgeInstance* parent) { if (onAddFx) onAddFx (parent); };
        }
        masterStrip->onFxReorderRequest = [this] (BridgeInstance* fx, int newIdx) {
            if (onFxReorderRequest) onFxReorderRequest (fx, newIdx);
        };
        masterStrip->resized();  // re-layout after bridge pointers are set (bounds may be unchanged)

        resized();
        repaint();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff14141f));

        auto header = getLocalBounds().removeFromTop (40);
        g.setColour (juce::Colour (0xff1a1a25));
        g.fillRect (header);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (Font (16.0f, Font::bold));
        g.drawText (LvhStr ("STR_MIXER_CONSOLE"),
                    header.reduced (12, 0).removeFromLeft (300),
                    Justification::centredLeft);

        if (strips.isEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.setFont (Font (13.0f));
            g.drawText (LvhStr ("STR_NO_INSTRUMENTS"),
                        getLocalBounds().withTrimmedTop (40).withTrimmedRight (120).reduced (20),
                        Justification::centred, true);
        }
    }

    void refresh()
    {
        repaint();   // redraws STR_MIXER_CONSOLE and STR_NO_INSTRUMENTS
        meterBtn->setTooltip (LvhStr ("STR_METER_TOOLTIP"));
        pinBtn_ ->setTooltip (LvhStr ("STR_PIN_TOOLTIP"));
        masterStrip->setDisplayName (LvhStr ("STR_MASTER"));
    }

    void setPinState (bool pinned)
    {
        pinBtn_->setToggleState (pinned, juce::dontSendNotification);
    }

    void setInputStripValues (float gain, bool muted)
    {
        if (inputStrip_) inputStrip_->setInitialValues (gain, 0.f, muted);
    }

    void resized() override
    {
        auto area   = getLocalBounds();
        auto header = area.removeFromTop (40);
        meterBtn->setBounds (header.removeFromRight (36).reduced (4));
        pinBtn_->setBounds  (header.removeFromRight (36).reduced (4));

        masterStrip->setBounds (area.removeFromRight (100).reduced (4));
        area.removeFromRight (4);
        if (inputStrip_) inputStrip_->setBounds (area.removeFromRight (80).reduced (2));
        area.removeFromRight (8);

        const int stripW = 80;
        for (auto* s : strips)
            s->setBounds (area.removeFromLeft (stripW).reduced (2));
    }

private:
    static constexpr int kMeterIntervalMs = 33;  // ~30 fps

    void timerCallback() override
    {
        for (int i = 0; i < strips.size() && i < bridges_.size(); ++i)
            strips[i]->updateMeter (bridges_[i]->exchangePeakL(),
                                    bridges_[i]->exchangePeakR());
        if (inputStrip_ != nullptr && getInputPeaks)
        {
            auto [l, r] = getInputPeaks();
            inputStrip_->updateMeter (l, r);
        }
    }

    void applyMeterVisibility()
    {
        bool v = meterBtn->getToggleState();
        for (auto* s : strips) s->setMeterVisible (v);
        if (inputStrip_) inputStrip_->setMeterVisible (v);
        masterStrip->setMeterVisible (v);

        // Stop the timer when meters are hidden to save CPU
        if (v) startTimer (kMeterIntervalMs);
        else   stopTimer();

        resized();
    }

    // Update solo-dimming on all strips.
    // Called when any solo button is toggled.
    void updateSoloDimming()
    {
        bool anySoloed = false;
        for (auto* s : strips)
            if (s->isSoloed()) { anySoloed = true; break; }

        for (auto* s : strips)
            s->setSoloDimmed (anySoloed && ! s->isSoloed());
    }

    std::unique_ptr<IconButton>      meterBtn;
    std::unique_ptr<IconButton>      pinBtn_;
    juce::OwnedArray<MixerStrip>     strips;
    std::unique_ptr<MixerStrip>      inputStrip_;
    std::unique_ptr<MixerStrip>      masterStrip;
    juce::Array<BridgeInstance*>     bridges_;   // parallel to strips[]

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerContentComponent)
};

// =====================================================================
// MixerWindow
// =====================================================================
class MixerWindow : public DocumentWindow
{
public:
    explicit MixerWindow (const juce::String& title)
        : DocumentWindow (title, juce::Colour (0xff14141f), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        content = new MixerContentComponent();
        content->onMasterGainChange = [this] (float v) {
            if (onMasterGainChange) onMasterGainChange (v);
        };
        content->onToggleFxWindow = [this] (BridgeInstance* b) {
            if (onToggleFxWindow) onToggleFxWindow (b);
        };
        content->onAddFx = [this] (BridgeInstance* parent) {
            if (onAddFx) onAddFx (parent);
        };
        content->onPinToggled = [this] (bool pinned) {
            setAlwaysOnTop (pinned);
        };
        content->onMidiLearnRequest = [this] (BridgeInstance* b, MixerParam p) {
            if (onMidiLearnRequest) onMidiLearnRequest (b, p);
        };
        content->onMidiClearMapping = [this] (BridgeInstance* b, MixerParam p) {
            if (onMidiClearMapping) onMidiClearMapping (b, p);
        };
        content->onFxReorderRequest = [this] (BridgeInstance* fx, int newIdx) {
            if (onFxReorderRequest) onFxReorderRequest (fx, newIdx);
        };
        content->onInputGainChange = [this] (float v) {
            if (onInputGainChange) onInputGainChange (v);
        };
        content->onInputMuteChange = [this] (bool m) {
            if (onInputMuteChange) onInputMuteChange (m);
        };
        content->getInputPeaks = [this] () -> std::pair<float,float> {
            return getInputPeaks ? getInputPeaks() : std::make_pair (0.f, 0.f);
        };
        setContentOwned (content, true);
        setResizable (true, false);
        centreWithSize (720, 480);
    }

    void setPinState (bool pinned)
    {
        setAlwaysOnTop (pinned);
        if (content != nullptr) content->setPinState (pinned);
    }

    void refresh()
    {
        if (content != nullptr) content->refresh();
    }

    void updateBridges (const juce::Array<BridgeInstance*>& instrumentBridges,
                        const juce::Array<BridgeInstance*>& effectBridges = {})
    {
        if (content != nullptr)
            content->updateBridges (instrumentBridges, effectBridges);
    }

    // Sync MASTER fader from Core slider (no callback loop)
    void setMasterGain (float v)
    {
        if (content != nullptr) content->setMasterGain (v);
    }

    void closeButtonPressed() override
    {
        if (onClose) onClose();
    }

    std::function<void()>                            onClose;
    std::function<void(float)>                       onMasterGainChange;
    std::function<void(BridgeInstance*)>             onToggleFxWindow;
    std::function<void(BridgeInstance*)>             onAddFx;
    std::function<void(BridgeInstance*, MixerParam)> onMidiLearnRequest;
    std::function<void(BridgeInstance*, MixerParam)> onMidiClearMapping;
    std::function<void(BridgeInstance*, int)>        onFxReorderRequest;
    std::function<void(float)>                       onInputGainChange;
    std::function<void(bool)>                        onInputMuteChange;
    std::function<std::pair<float,float>()>          getInputPeaks;

    void setInputStripValues (float gain, bool muted)
    {
        if (content) content->setInputStripValues (gain, muted);
    }

    // Called by UIManager to forward learn state / CC values to the correct strip
    void setStripLearnMode (BridgeInstance* b, MixerParam p, bool active)
    {
        if (content) content->setStripLearnMode (b, p, active);
    }
    void applyMidiValue (BridgeInstance* b, MixerParam p, float value)
    {
        if (content) content->applyMidiValue (b, p, value);
    }

private:
    MixerContentComponent* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerWindow)
};
