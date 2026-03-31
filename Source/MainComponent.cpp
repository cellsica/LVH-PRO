#include "MainComponent.h"

MainComponent::MainComponent (MidiKeyboardState& state)
{
    lvhLogo = std::make_unique<LvhLogoLabel>();
    addAndMakeVisible (*lvhLogo);
    lvhLogo->onRightClick = [this] { if (onLogoRightClick) onLogoRightClick(); };

    panicButton = std::make_unique<IconButton> ("PANIC (All Notes Off)", Icons::panic);
    addAndMakeVisible (*panicButton);
    panicButton->setColour (TextButton::buttonColourId, Colours::red);
    panicButton->onClick = [this] { if (onPanicClicked) onPanicClicked(); };

    kbdToggleButton = std::make_unique<IconButton> ("Toggle Keyboard", Icons::kbd);
    addAndMakeVisible (*kbdToggleButton);
    kbdToggleButton->setClickingTogglesState (true);
    kbdToggleButton->setToggleState (true, dontSendNotification);
    kbdToggleButton->setColour (TextButton::buttonColourId,   Colour (0xff333344));
    kbdToggleButton->setColour (TextButton::buttonOnColourId, Colour (0xff2a6030));
    kbdToggleButton->onClick = [this] { toggleKeyboard(); };

    monitorToggleButton = std::make_unique<IconButton> ("Toggle MIDI Monitor", Icons::monitor);
    addAndMakeVisible (*monitorToggleButton);
    monitorToggleButton->setClickingTogglesState (true);
    monitorToggleButton->setToggleState (true, dontSendNotification);
    monitorToggleButton->setColour (TextButton::buttonColourId,   Colour (0xff333344));
    monitorToggleButton->setColour (TextButton::buttonOnColourId, Colour (0xff2a5060));
    monitorToggleButton->onClick = [this] { toggleMonitor(); };

    mixerToggleButton = std::make_unique<IconButton> ("Toggle Mixer Console", Icons::mixer);
    addAndMakeVisible (*mixerToggleButton);
    mixerToggleButton->setClickingTogglesState (true);
    mixerToggleButton->setToggleState (false, dontSendNotification);
    mixerToggleButton->setColour (TextButton::buttonColourId,   Colour (0xff333344));
    mixerToggleButton->setColour (TextButton::buttonOnColourId, Colour (0xff604020));
    mixerToggleButton->onClick = [this] { toggleMixer(); };

    stageToggleButton = std::make_unique<IconButton> ("Open Stage Set", Icons::stage);
    addAndMakeVisible (*stageToggleButton);
    stageToggleButton->setClickingTogglesState (true);
    stageToggleButton->setToggleState (false, dontSendNotification);
    stageToggleButton->setColour (TextButton::buttonColourId,   Colour (0xff333344));
    stageToggleButton->setColour (TextButton::buttonOnColourId, Colour (0xff3a2060));
    stageToggleButton->onClick = [this] { toggleStage(); };

    metronomeToggleButton = std::make_unique<IconButton> ("Open Metronome", Icons::metronome);
    addAndMakeVisible (*metronomeToggleButton);
    metronomeToggleButton->setClickingTogglesState (true);
    metronomeToggleButton->setToggleState (false, dontSendNotification);
    metronomeToggleButton->setColour (TextButton::buttonColourId,   Colour (0xff333344));
    metronomeToggleButton->setColour (TextButton::buttonOnColourId, Colour (0xff205040));
    metronomeToggleButton->onClick = [this] { toggleMetronome(); };

    vuMeterToggleButton = std::make_unique<IconButton> ("Open VU Meter", Icons::vuMeter);
    addAndMakeVisible (*vuMeterToggleButton);
    vuMeterToggleButton->setClickingTogglesState (true);
    vuMeterToggleButton->setToggleState (false, dontSendNotification);
    vuMeterToggleButton->setColour (TextButton::buttonColourId,   Colour (0xff333344));
    vuMeterToggleButton->setColour (TextButton::buttonOnColourId, Colour (0xff3a2850));
    vuMeterToggleButton->onClick = [this] { toggleVuMeter(); };

    visualizerToggleButton = std::make_unique<IconButton> ("Open Visualizer", Icons::visualizer);
    addAndMakeVisible (*visualizerToggleButton);
    visualizerToggleButton->setClickingTogglesState (true);
    visualizerToggleButton->setToggleState (false, dontSendNotification);
    visualizerToggleButton->setColour (TextButton::buttonColourId,   Colour (0xff333344));
    visualizerToggleButton->setColour (TextButton::buttonOnColourId, Colour (0xff1a3a50));
    visualizerToggleButton->onClick = [this] { toggleVisualizer(); };

    octaveDownButton = std::make_unique<TextButton> ("-");
    addAndMakeVisible (*octaveDownButton);
    octaveDownButton->setTooltip ("Octave Down (shortcut: [)");
    octaveDownButton->setColour (TextButton::buttonColourId, Colour (0xff333344));
    octaveDownButton->onClick = [this] { if (onOctaveShift) onOctaveShift (-1); };

    octaveLabel = std::make_unique<Label>();
    addAndMakeVisible (*octaveLabel);
    octaveLabel->setColour (Label::textColourId, Colour (0xffaaaacc));
    octaveLabel->setFont (Font (11.0f, Font::bold));
    octaveLabel->setJustificationType (Justification::centred);
    octaveLabel->setText ("Oct 4", dontSendNotification);

    octaveUpButton = std::make_unique<TextButton> ("+");
    addAndMakeVisible (*octaveUpButton);
    octaveUpButton->setTooltip ("Octave Up (shortcut: ])");
    octaveUpButton->setColour (TextButton::buttonColourId, Colour (0xff333344));
    octaveUpButton->onClick = [this] { if (onOctaveShift) onOctaveShift (+1); };

    speakerButton = std::make_unique<SpeakerButton>();
    addAndMakeVisible (*speakerButton);
    speakerButton->setColour (TextButton::buttonColourId, Colour (0xff333344));

    volumeSlider = std::make_unique<Slider> (Slider::LinearHorizontal, Slider::NoTextBox);
    addAndMakeVisible (*volumeSlider);
    volumeSlider->setRange (0.0, 1.0);
    volumeSlider->setValue (1.0, dontSendNotification);
    volumeSlider->setTooltip ("Master Volume");
    volumeSlider->onValueChange = [this] {
        double v = volumeSlider->getValue();
        setVolumeDisplay (v);
        if (onVolumeChanged) onVolumeChanged (v);
    };

    volumeValueLabel = std::make_unique<Label>();
    addAndMakeVisible (*volumeValueLabel);
    volumeValueLabel->setText ("100%", dontSendNotification);
    volumeValueLabel->setFont (Font (10.0f, Font::bold));
    volumeValueLabel->setJustificationType (Justification::centred);
    volumeValueLabel->setColour (Label::textColourId, Colour (0xffaaaacc));
    volumeValueLabel->setColour (Label::backgroundColourId, Colour (0xff1e1e2e));

    levelMeter = std::make_unique<LevelMeter>();
    addAndMakeVisible (*levelMeter);

    midiMonitorLabel = std::make_unique<Label>();
    addAndMakeVisible (*midiMonitorLabel);
    midiMonitorLabel->setColour (Label::textColourId, Colour (0xff88ccff));
    midiMonitorLabel->setFont (Font (11.0f));
    midiMonitorLabel->setJustificationType (Justification::centredLeft);
    midiMonitorLabel->setText ("No MIDI", dontSendNotification);

    systemLogPanel = std::make_unique<SystemLogPanel>();
    addAndMakeVisible (*systemLogPanel);

    monitorPanel = std::make_unique<InfoMonitorPanel>();
    addAndMakeVisible (*monitorPanel);

    keyboardComponent = std::make_unique<PcKeyboardComponent> (state);
    addAndMakeVisible (*keyboardComponent);

    // Top resizer: drags monitorHeight up/down; keyboard is unaffected;
    //              systemLog absorbs the remaining space.
    resizerBar = std::make_unique<HResizer>();
    addAndMakeVisible (*resizerBar);
    resizerBar->onDrag = [this] (int delta) {
        // Drag DOWN (positive delta) → bar moves down → monitorPanel shrinks
        lastMonitorHeight = jmax (kMinMidiH, jmin (400, lastMonitorHeight - delta));
        resized();
    };

    // Bottom resizer: drags keyboardHeight up/down; MIDILog is unaffected;
    //                 systemLog absorbs the remaining space.
    resizerBar2 = std::make_unique<HResizer>();
    addAndMakeVisible (*resizerBar2);
    resizerBar2->onDrag = [this] (int delta) {
        // Drag DOWN (positive delta) → bar moves down → keyboard shrinks
        lastKeyboardHeight = jmax (kMinKbdH, jmin (300, lastKeyboardHeight - delta));
        resized();
    };

    scanOverlay = std::make_unique<ScanOverlay>();
    addChildComponent (*scanOverlay);

    setSize (900, 600);
}

// ── Toggle helpers ─────────────────────────────────────────────────────────

void MainComponent::toggleMonitor()
{
    monitorVisible = ! monitorVisible;
    monitorToggleButton->setToggleState (monitorVisible, dontSendNotification);
    resized();
}

void MainComponent::toggleKeyboard()
{
    keyboardVisible = ! keyboardVisible;
    kbdToggleButton->setToggleState (keyboardVisible, dontSendNotification);
    resized();
}

void MainComponent::toggleMixer()
{
    if (onMixerToggle)
        onMixerToggle (mixerToggleButton->getToggleState());
}

void MainComponent::toggleStage()
{
    if (onStageToggle)
        onStageToggle (stageToggleButton->getToggleState());
}

void MainComponent::toggleMetronome()
{
    if (onMetronomeToggle)
        onMetronomeToggle (metronomeToggleButton->getToggleState());
}

void MainComponent::toggleVuMeter()
{
    if (onVuMeterToggle)
        onVuMeterToggle (vuMeterToggleButton->getToggleState());
}

void MainComponent::toggleVisualizer()
{
    if (onVisualizerToggle)
        onVisualizerToggle (visualizerToggleButton->getToggleState());
}

// ── Scan overlay ───────────────────────────────────────────────────────────

void MainComponent::showScanOverlay()
{
    scanOverlay->setBounds (getLocalBounds());
    scanOverlay->setProgress ("Preparing...");
    scanOverlay->setVisible (true);
    scanOverlay->toFront (false);
    repaint();
}

void MainComponent::hideScanOverlay()                    { scanOverlay->setVisible (false); }
void MainComponent::updateScanProgress (const String& f) { scanOverlay->setProgress (f); }

// ── Accessors ──────────────────────────────────────────────────────────────

void MainComponent::setMidiMonitorText (const String& t) { midiMonitorLabel->setText (t, dontSendNotification); }
void MainComponent::pushSystemMessage  (const String& t) { systemLogPanel->pushMessage (t); }

Slider&           MainComponent::getVolumeSlider()   { return *volumeSlider; }
void              MainComponent::setVolumeDisplay (double v)
{
    volumeValueLabel->setText (juce::String (juce::roundToInt (v * 100)) + "%",
                               dontSendNotification);
}

LevelMeter&       MainComponent::getLevelMeter()     { return *levelMeter; }
SpeakerButton&    MainComponent::getSpeakerButton()  { return *speakerButton; }
InfoMonitorPanel& MainComponent::getMonitorPanel()   { return *monitorPanel; }

void MainComponent::setOctaveDisplay (int octaveNumber)
{
    octaveLabel->setText ("Oct " + String (octaveNumber), dontSendNotification);
}

PcKeyboardComponent& MainComponent::getKeyboardComponent() { return *keyboardComponent; }

void MainComponent::setLevelMeterVisible (bool v)
{
    showLevelMeter = v;
    levelMeter->setVisible (v);
    resized();
}

void MainComponent::setMidiMonitorVisible (bool v)
{
    showMidiMonitor = v;
    midiMonitorLabel->setVisible (v);
    resized();
}

void MainComponent::setMonitorPanelVisible (bool v)
{
    monitorVisible = v;
    monitorToggleButton->setToggleState (v, dontSendNotification);
    resized();
}

void MainComponent::setMixerWindowVisible (bool v)
{
    mixerToggleButton->setToggleState (v, dontSendNotification);
}

void MainComponent::setStageWindowVisible (bool v)
{
    stageToggleButton->setToggleState (v, dontSendNotification);
}

void MainComponent::setMetronomeWindowVisible (bool v)
{
    metronomeToggleButton->setToggleState (v, dontSendNotification);
}

void MainComponent::setVuMeterWindowVisible (bool v)
{
    vuMeterToggleButton->setToggleState (v, dontSendNotification);
}

void MainComponent::setVisualizerWindowVisible (bool v)
{
    visualizerToggleButton->setToggleState (v, dontSendNotification);
}


// ── Paint ──────────────────────────────────────────────────────────────────

void MainComponent::paint (Graphics& g)
{
    g.fillAll (Colour (0xff14141f));
    g.setColour (Colour (0xff252535));
    g.fillRect (getLocalBounds().removeFromTop (44));
}

// ── Resized ────────────────────────────────────────────────────────────────
//
// Size-change rules:
//   top bar drag    → lastMonitorHeight  changes; keyboard fixed; systemLog absorbs
//   bottom bar drag → lastKeyboardHeight changes; MIDILog  fixed; systemLog absorbs
//
void MainComponent::resized()
{
    auto area        = getLocalBounds();
    auto toolbarArea = area.removeFromTop (44);

    // ── Toolbar ──────────────────────────────────────────────────────
    lvhLogo->setBounds (toolbarArea.removeFromLeft (60));
    auto toolbar = toolbarArea.reduced (2, 4);

    panicButton        ->setBounds (toolbar.removeFromLeft (36).reduced (2));
    kbdToggleButton    ->setBounds (toolbar.removeFromLeft (36).reduced (2));
    monitorToggleButton->setBounds (toolbar.removeFromLeft (36).reduced (2));
    mixerToggleButton     ->setBounds (toolbar.removeFromLeft (36).reduced (2));
    stageToggleButton     ->setBounds (toolbar.removeFromLeft (36).reduced (2));
    metronomeToggleButton ->setBounds (toolbar.removeFromLeft (36).reduced (2));
    vuMeterToggleButton   ->setBounds (toolbar.removeFromLeft (36).reduced (2));
    visualizerToggleButton->setBounds (toolbar.removeFromLeft (36).reduced (2));

    toolbar.removeFromLeft (6); // small gap
    octaveDownButton   ->setBounds (toolbar.removeFromLeft (26).reduced (2));
    octaveLabel        ->setBounds (toolbar.removeFromLeft (40).reduced (2));
    octaveUpButton     ->setBounds (toolbar.removeFromLeft (26).reduced (2));

    speakerButton   ->setBounds (toolbar.removeFromRight (36).reduced (2));
    volumeValueLabel->setBounds (toolbar.removeFromRight (34).reduced (1, 4));
    volumeSlider    ->setBounds (toolbar.removeFromRight (80).reduced (2));
    if (showLevelMeter)  levelMeter      ->setBounds (toolbar.removeFromRight (110).reduced (2, 4));
    if (showMidiMonitor) midiMonitorLabel->setBounds (toolbar.removeFromRight (160).reduced (2));

    // ── Content area ─────────────────────────────────────────────────
    auto content = area.reduced (4);
    const int totalH = content.getHeight();

    // How much height the fixed panels + resizers consume
    int fixedH = 0;
    if (monitorVisible)  fixedH += kResizerH + jmax (kMinMidiH, lastMonitorHeight);
    if (keyboardVisible) fixedH += kResizerH + jmax (kMinKbdH,  lastKeyboardHeight);

    // SystemLog takes all remaining space (never below kMinLogH)
    int sysH = jmax (kMinLogH, totalH - fixedH);

    auto c = content;

    systemLogPanel->setBounds (c.removeFromTop (sysH));

    if (monitorVisible)
    {
        resizerBar->setBounds (c.removeFromTop (kResizerH));
        monitorPanel->setBounds (c.removeFromTop (lastMonitorHeight));
    }

    if (keyboardVisible)
    {
        resizerBar2->setBounds (c.removeFromTop (kResizerH));
        keyboardComponent->setBounds (c.removeFromTop (lastKeyboardHeight));
    }

    resizerBar ->setVisible (monitorVisible);
    monitorPanel->setVisible (monitorVisible);
    resizerBar2->setVisible (keyboardVisible);
    keyboardComponent->setVisible (keyboardVisible);

    scanOverlay->setBounds (getLocalBounds());
}
