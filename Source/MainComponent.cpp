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

    speakerButton = std::make_unique<SpeakerButton>();
    addAndMakeVisible (*speakerButton);
    speakerButton->setColour (TextButton::buttonColourId, Colour (0xff333344));

    volumeSlider = std::make_unique<Slider> (Slider::LinearHorizontal, Slider::NoTextBox);
    addAndMakeVisible (*volumeSlider);
    volumeSlider->setRange (0.0, 1.0);
    volumeSlider->setValue (1.0, dontSendNotification);
    volumeSlider->setTooltip ("Volume");

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
LevelMeter&       MainComponent::getLevelMeter()     { return *levelMeter; }
SpeakerButton&    MainComponent::getSpeakerButton()  { return *speakerButton; }
InfoMonitorPanel& MainComponent::getMonitorPanel()   { return *monitorPanel; }

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

    speakerButton->setBounds (toolbar.removeFromRight (36).reduced (2));
    volumeSlider ->setBounds (toolbar.removeFromRight (80).reduced (2));
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
