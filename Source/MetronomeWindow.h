#pragma once
#include "UiCommon.h"
#include "UiComponents.h"

// =====================================================================
// MetronomeContentComponent
// =====================================================================
class MetronomeContentComponent : public juce::Component
{
public:
    std::function<void(bool)>   onPlayStopChanged;
    std::function<void(double)> onBpmChanged;
    std::function<void(float)>  onVolumeChanged;
    std::function<void()>       onTapTempo;
    std::function<void(int)>    onBeatsPerBarChanged;
    std::function<void(bool)>   onPinToggled;

    ~MetronomeContentComponent() override
    {
        bpbDown_   .setLookAndFeel (nullptr);
        bpbUp_     .setLookAndFeel (nullptr);
        bpmMinus10_.setLookAndFeel (nullptr);
        bpmMinus1_ .setLookAndFeel (nullptr);
        bpmPlus1_  .setLookAndFeel (nullptr);
        bpmPlus10_ .setLookAndFeel (nullptr);
    }

    MetronomeContentComponent()
    {
        // Pin button
        pinBtn_ = std::make_unique<IconButton> ("Always on Top", Icons::pin);
        pinBtn_->setClickingTogglesState (true);
        pinBtn_->setColour (TextButton::buttonColourId,   juce::Colour (0xff252535));
        pinBtn_->setColour (TextButton::buttonOnColourId, juce::Colour (0xffaa6600));
        pinBtn_->setTooltip ("Pin window on top");
        pinBtn_->onClick = [this] {
            if (onPinToggled) onPinToggled (pinBtn_->getToggleState());
        };
        addAndMakeVisible (*pinBtn_);

        // Play / Stop toggle
        playBtn_.setButtonText (kLabelPlay);
        playBtn_.setClickingTogglesState (true);
        playBtn_.setColour (TextButton::buttonColourId,   juce::Colour (0xff2a5a2a));
        playBtn_.setColour (TextButton::buttonOnColourId, juce::Colour (0xff44aa44));
        playBtn_.onClick = [this] {
            bool playing = playBtn_.getToggleState();
            playBtn_.setButtonText (playing ? kLabelStop : kLabelPlay);
            if (onPlayStopChanged) onPlayStopChanged (playing);
        };
        addAndMakeVisible (playBtn_);

        // Tap Tempo
        tapBtn_.setButtonText ("TAP");
        tapBtn_.setColour (TextButton::buttonColourId, juce::Colour (0xff2a3a55));
        tapBtn_.onClick = [this] { if (onTapTempo) onTapTempo(); };
        addAndMakeVisible (tapBtn_);

        // BPM label
        bpmLabel_.setText ("BPM", juce::dontSendNotification);
        bpmLabel_.setFont (juce::Font (10.f, juce::Font::bold));
        bpmLabel_.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        addAndMakeVisible (bpmLabel_);

        // BPM value (editable)
        bpmValue_.setText ("120", juce::dontSendNotification);
        bpmValue_.setFont (juce::Font (13.f, juce::Font::bold));
        bpmValue_.setJustificationType (juce::Justification::centred);
        bpmValue_.setEditable (false, true, false);
        bpmValue_.setColour (juce::Label::textColourId,       juce::Colours::white);
        bpmValue_.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1a1a2a));
        bpmValue_.onEditorHide = [this] {
            double v = juce::jlimit (40.0, 240.0, bpmValue_.getText().getDoubleValue());
            bpmValue_.setText (juce::String (juce::roundToInt (v)), juce::dontSendNotification);
            bpmSlider_.setValue (v, juce::dontSendNotification);
            if (onBpmChanged) onBpmChanged (v);
        };
        addAndMakeVisible (bpmValue_);

        // BPM slider
        bpmSlider_.setSliderStyle (juce::Slider::LinearHorizontal);
        bpmSlider_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        bpmSlider_.setRange (40.0, 240.0, 1.0);
        bpmSlider_.setValue (120.0, juce::dontSendNotification);
        bpmSlider_.onValueChange = [this] {
            double v = bpmSlider_.getValue();
            bpmValue_.setText (juce::String (juce::roundToInt (v)), juce::dontSendNotification);
            if (onBpmChanged) onBpmChanged (v);
        };
        addAndMakeVisible (bpmSlider_);

        // BPM adjustment buttons: [-10] [-1] [+1] [+10]
        auto setupAdjBtn = [this] (juce::TextButton& btn, const juce::String& text, double delta)
        {
            btn.setButtonText (text);
            btn.setColour (TextButton::buttonColourId, juce::Colour (0xff2a2a3e));
            btn.setLookAndFeel (&bpmAdjLF_);
            btn.onClick = [this, delta] { adjustBpm (delta); };
            addAndMakeVisible (btn);
        };
        setupAdjBtn (bpmMinus10_, "-10", -10.0);
        setupAdjBtn (bpmMinus1_,  "-1",   -1.0);
        setupAdjBtn (bpmPlus1_,   "+1",   +1.0);
        setupAdjBtn (bpmPlus10_,  "+10", +10.0);

        // Volume label
        volLabel_.setText ("VOL", juce::dontSendNotification);
        volLabel_.setFont (juce::Font (10.f, juce::Font::bold));
        volLabel_.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        addAndMakeVisible (volLabel_);

        // Volume slider
        volSlider_.setSliderStyle (juce::Slider::LinearHorizontal);
        volSlider_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volSlider_.setRange (0.0, 1.0);
        volSlider_.setValue (0.7, juce::dontSendNotification);
        volSlider_.onValueChange = [this] {
            if (onVolumeChanged) onVolumeChanged ((float) volSlider_.getValue());
        };
        addAndMakeVisible (volSlider_);

        // Beats-per-bar: "-" value "+"
        bpbDown_.setButtonText ("-");
        bpbDown_.setColour (TextButton::buttonColourId, juce::Colour (0xff333344));
        bpbDown_.onClick = [this] { setBeatsPerBarUI (beatsPerBar_ - 1); };
        bpbDown_.setLookAndFeel (&bpbLF_);
        addAndMakeVisible (bpbDown_);

        bpbUp_.setButtonText ("+");
        bpbUp_.setColour (TextButton::buttonColourId, juce::Colour (0xff333344));
        bpbUp_.onClick = [this] { setBeatsPerBarUI (beatsPerBar_ + 1); };
        bpbUp_.setLookAndFeel (&bpbLF_);
        addAndMakeVisible (bpbUp_);

        bpbLabel_.setText ("4", juce::dontSendNotification);
        bpbLabel_.setFont (juce::Font (12.f, juce::Font::bold));
        bpbLabel_.setJustificationType (juce::Justification::centred);
        bpbLabel_.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (bpbLabel_);

        bpbCaption_.setText ("BEATS/BAR", juce::dontSendNotification);
        bpbCaption_.setFont (juce::Font (10.f, juce::Font::bold));
        bpbCaption_.setJustificationType (juce::Justification::centredLeft);
        bpbCaption_.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        addAndMakeVisible (bpbCaption_);
    }

    // ── Called by UIManager (message thread) ──────────────────────────────

    void updateBeat (int beatInBar)
    {
        currentBeat_ = beatInBar;
        repaint();
    }

    void setPlayState (bool playing)
    {
        playBtn_.setToggleState (playing, juce::dontSendNotification);
        playBtn_.setButtonText (playing ? kLabelStop : kLabelPlay);
    }

    void setBpm (double bpm)
    {
        bpmSlider_.setValue (bpm, juce::dontSendNotification);
        bpmValue_.setText (juce::String (juce::roundToInt (bpm)), juce::dontSendNotification);
    }

    void setVolume (float v)
    {
        volSlider_.setValue ((double) v, juce::dontSendNotification);
    }

    void setBeatsPerBar (int b)
    {
        beatsPerBar_ = juce::jlimit (1, kMaxBeats, b);
        bpbLabel_.setText (juce::String (beatsPerBar_), juce::dontSendNotification);
        if (currentBeat_ >= beatsPerBar_) currentBeat_ = -1;
        repaint();
    }

    void setPinState (bool p) { pinBtn_->setToggleState (p, juce::dontSendNotification); }

    // ── Paint ─────────────────────────────────────────────────────────────

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff14141f));

        // Header
        auto header = getLocalBounds().removeFromTop (kHeaderH);
        g.setColour (juce::Colour (0xff1a1a25));
        g.fillRect (header);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (juce::Font (13.f, juce::Font::bold));
        g.drawText ("METRONOME", header.reduced (10, 0).removeFromLeft (160),
                    juce::Justification::centredLeft);

        // LED beat indicators
        drawLeds (g);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (kHeaderH);
        pinBtn_->setBounds (header.removeFromRight (30).reduced (3));

        area.reduce (10, 0);
        area.removeFromTop (kLedAreaH + 10);  // space for LED

        // Play + Tap row
        auto row = area.removeFromTop (32);
        playBtn_.setBounds (row.removeFromLeft (88).reduced (2));
        row.removeFromLeft (6);
        tapBtn_.setBounds  (row.removeFromLeft (88).reduced (2));
        area.removeFromTop (6);

        // BPM row: label | value | (space)
        auto bpmRow = area.removeFromTop (18);
        bpmLabel_.setBounds (bpmRow.removeFromLeft (32));
        bpmValue_.setBounds (bpmRow.removeFromLeft (54).reduced (1, 0));
        area.removeFromTop (2);

        // BPM adj+slider row: [-10][-1][slider][+1][+10]
        auto sliderRow = area.removeFromTop (18);
        bpmMinus10_.setBounds (sliderRow.removeFromLeft (28).reduced (1));
        bpmMinus1_ .setBounds (sliderRow.removeFromLeft (22).reduced (1));
        bpmPlus10_ .setBounds (sliderRow.removeFromRight (28).reduced (1));
        bpmPlus1_  .setBounds (sliderRow.removeFromRight (22).reduced (1));
        bpmSlider_ .setBounds (sliderRow);
        area.removeFromTop (8);

        // VOL row: label | slider
        auto volRow = area.removeFromTop (18);
        volLabel_.setBounds (volRow.removeFromLeft (32));
        area.removeFromTop (2);
        volSlider_.setBounds (area.removeFromTop (18));
        area.removeFromTop (8);

        // Beats-per-bar row: [BEATS/BAR] [–] [N] [+]
        auto bpbRow = area.removeFromTop (20);
        bpbCaption_.setBounds (bpbRow.removeFromLeft (64));
        bpbRow.removeFromLeft (6);
        bpbDown_ .setBounds (bpbRow.removeFromLeft (20).reduced (1));
        bpbLabel_.setBounds (bpbRow.removeFromLeft (26));
        bpbUp_   .setBounds (bpbRow.removeFromLeft (20).reduced (1));
    }

private:
    static constexpr int kHeaderH  = 34;
    static constexpr int kLedAreaH = 26;
    static constexpr int kMaxBeats = 8;
    static constexpr const char* kLabelPlay = "PLAY";
    static constexpr const char* kLabelStop = "STOP";

    void adjustBpm (double delta)
    {
        double v = juce::jlimit (40.0, 240.0, bpmSlider_.getValue() + delta);
        bpmSlider_.setValue (v, juce::dontSendNotification);
        bpmValue_.setText (juce::String (juce::roundToInt (v)), juce::dontSendNotification);
        if (onBpmChanged) onBpmChanged (v);
    }

    void setBeatsPerBarUI (int b)
    {
        int clamped = juce::jlimit (1, kMaxBeats, b);
        setBeatsPerBar (clamped);
        if (onBeatsPerBarChanged) onBeatsPerBarChanged (beatsPerBar_);
    }

    void drawLeds (juce::Graphics& g)
    {
        // LED strip sits just below the header
        auto area = getLocalBounds()
                        .withTrimmedTop   (kHeaderH + 4)
                        .withTrimmedLeft  (10)
                        .withTrimmedRight (10)
                        .removeFromTop    (kLedAreaH);

        int n    = juce::jlimit (1, kMaxBeats, beatsPerBar_);
        int ledW = juce::jmin (36, (area.getWidth() - (n - 1) * 4) / n);
        int x    = area.getX();

        for (int i = 0; i < n; ++i)
        {
            juce::Rectangle<float> led ((float) x, (float) area.getY(),
                                        (float) ledW, (float) area.getHeight());
            bool active = (i == currentBeat_);
            bool isHi   = (i == 0);

            juce::Colour col = active
                ? (isHi ? juce::Colour (0xffff7722) : juce::Colour (0xff33bbff))
                : juce::Colour (0xff1c1c2c);

            g.setColour (col);
            g.fillRoundedRectangle (led, 3.f);
            g.setColour (juce::Colour (0xff2a2a40));
            g.drawRoundedRectangle (led, 3.f, 1.f);

            x += ledW + 4;
        }
    }

    // LookAndFeel for BEATS/BAR [-][+] buttons — bumps the font up to 15pt bold
    struct BpbButtonLF : public juce::LookAndFeel_V4
    {
        juce::Font getTextButtonFont (juce::TextButton&, int) override
        {
            return juce::Font (15.f, juce::Font::bold);
        }
    } bpbLF_;

    // LookAndFeel for BPM adjustment buttons — small bold font
    struct BpmAdjLF : public juce::LookAndFeel_V4
    {
        juce::Font getTextButtonFont (juce::TextButton&, int) override
        {
            return juce::Font (9.f, juce::Font::bold);
        }
    } bpmAdjLF_;

    int beatsPerBar_ = 4;
    int currentBeat_ = -1;

    std::unique_ptr<IconButton> pinBtn_;
    juce::TextButton  playBtn_, tapBtn_;
    juce::TextButton  bpbDown_, bpbUp_;
    juce::TextButton  bpmMinus10_, bpmMinus1_, bpmPlus1_, bpmPlus10_;
    juce::Label       bpmLabel_, bpmValue_, volLabel_;
    juce::Label       bpbLabel_, bpbCaption_;
    juce::Slider      bpmSlider_, volSlider_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomeContentComponent)
};

// =====================================================================
// MetronomeWindow
// =====================================================================
class MetronomeWindow : public juce::DocumentWindow
{
public:
    MetronomeWindow()
        : juce::DocumentWindow ("Metronome",
                                juce::Colour (0xff14141f),
                                juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        content_ = new MetronomeContentComponent();
        content_->onPinToggled = [this] (bool p) { setAlwaysOnTop (p); };
        setContentOwned (content_, true);
        setResizable (false, false);
        centreWithSize (300, 268);
    }

    // ── Update from UIManager (message thread) ────────────────────────────

    void updateBeat    (int beat)   { if (content_) content_->updateBeat    (beat); }
    void setPlayState  (bool p)     { if (content_) content_->setPlayState  (p); }
    void setBpm        (double bpm) { if (content_) content_->setBpm        (bpm); }
    void setVolume     (float v)    { if (content_) content_->setVolume     (v); }
    void setBeatsPerBar (int b)     { if (content_) content_->setBeatsPerBar (b); }

    void setPinState (bool p)
    {
        setAlwaysOnTop (p);
        if (content_) content_->setPinState (p);
    }

    // ── Callbacks — wired by UIManager ────────────────────────────────────

    std::function<void(bool)>   onPlayStopChanged;
    std::function<void(double)> onBpmChanged;
    std::function<void(float)>  onVolumeChanged;
    std::function<void()>       onTapTempo;
    std::function<void(int)>    onBeatsPerBarChanged;
    std::function<void()>       onClose;

    void wireCallbacks()
    {
        if (content_ == nullptr) return;
        content_->onPlayStopChanged   = [this] (bool p)   { if (onPlayStopChanged)   onPlayStopChanged (p); };
        content_->onBpmChanged        = [this] (double b)  { if (onBpmChanged)        onBpmChanged (b); };
        content_->onVolumeChanged     = [this] (float v)   { if (onVolumeChanged)     onVolumeChanged (v); };
        content_->onTapTempo          = [this] ()           { if (onTapTempo)          onTapTempo(); };
        content_->onBeatsPerBarChanged = [this] (int b)    { if (onBeatsPerBarChanged) onBeatsPerBarChanged (b); };
    }

    void closeButtonPressed() override
    {
        if (onClose) onClose();
    }

private:
    MetronomeContentComponent* content_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomeWindow)
};
