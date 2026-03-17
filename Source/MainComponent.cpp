#include "MainComponent.h"

MainComponent::MainComponent (MidiKeyboardState& state)
{
    lvhLogo = std::make_unique<LvhLogoLabel>();
    addAndMakeVisible (*lvhLogo);
    lvhLogo->onRightClick = [this] { if (onLogoRightClick) onLogoRightClick(); };

    keyboardComponent = std::make_unique<PcKeyboardComponent> (state);
    addAndMakeVisible (*keyboardComponent);

    pluginButton = std::make_unique<TextButton> (currentPluginName);
    addAndMakeVisible (*pluginButton);
    pluginButton->onClick = [this] { if (onPluginMenuRequest) onPluginMenuRequest(); };

    presetButton = std::make_unique<IconButton> ("Preset", Icons::preset);
    addAndMakeVisible (*presetButton);
    presetButton->setColour (TextButton::buttonColourId, Colour (0xff226060));
    presetButton->setEnabled (false);
    presetButton->onClick = [this] { if (onPresetMenuRequest) onPresetMenuRequest(); };

    loadButton = std::make_unique<IconButton> ("Load Plugin", Icons::load);
    addAndMakeVisible (*loadButton);
    loadButton->setColour (TextButton::buttonColourId, Colour (0xff3a7bd5));
    loadButton->onClick = [this] { if (onLoadClicked) onLoadClicked(); };

    unloadButton = std::make_unique<IconButton> ("Unload Plugin", Icons::unload);
    addAndMakeVisible (*unloadButton);
    unloadButton->setColour (TextButton::buttonColourId, Colour (0xff882222));
    unloadButton->setEnabled (false);
    unloadButton->onClick = [this] { if (onUnloadClicked) onUnloadClicked(); };

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

    monitorToggleButton = std::make_unique<IconButton> ("Toggle Info Monitor", Icons::monitor);
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

    monitorPanel = std::make_unique<InfoMonitorPanel>();
    addAndMakeVisible (*monitorPanel);

    pluginViewport = std::make_unique<Viewport>();
    addAndMakeVisible (*pluginViewport);
    pluginViewport->setScrollBarsShown (true, true);

    // 5-slot layout: viewport | resizer1 | monitor | resizer2 | keyboard
    stretchLayout.setItemLayout (0, 100, -1.0, -1.0);
    stretchLayout.setItemLayout (1, 0, 8, 0);
    stretchLayout.setItemLayout (2, 0, 400, 0);
    stretchLayout.setItemLayout (3, 8, 8, 8);
    stretchLayout.setItemLayout (4, 50, 300, 50);
    resizerBar  = std::make_unique<StretchableLayoutResizerBar> (&stretchLayout, 1, false);
    resizerBar2 = std::make_unique<StretchableLayoutResizerBar> (&stretchLayout, 3, false);
    addAndMakeVisible (*resizerBar);
    addAndMakeVisible (*resizerBar2);

    scanOverlay = std::make_unique<ScanOverlay>();
    addChildComponent (*scanOverlay);

    setSize (900, 600);
}

void MainComponent::setPluginEditor (AudioProcessorEditor* editor)
{
    pluginViewport->setViewedComponent (editor, false);
    if (editor != nullptr)
    {
        editor->setVisible (true);
        resizeToFitEditor (editor);
    }
}

void MainComponent::resizeToFitEditor (AudioProcessorEditor* editor)
{
    if (editor == nullptr) return;
    int monitorArea = monitorVisible  ? (8 + lastMonitorHeight)  : 0;
    int kbArea      = keyboardVisible ? (8 + lastKeyboardHeight) : 0;
    int newHeight = 44 + 8 + editor->getHeight() + monitorArea + kbArea;
    if (auto* tlc = getTopLevelComponent())
        tlc->setSize (jmax (600, editor->getWidth() + 12), jmax (400, newHeight));
}

void MainComponent::toggleMonitor()
{
    monitorVisible = ! monitorVisible;
    monitorToggleButton->setToggleState (monitorVisible, dontSendNotification);
    int delta = (monitorVisible ? 1 : -1) * (lastMonitorHeight + 8);
    if (auto* tlc = getTopLevelComponent())
        tlc->setSize (tlc->getWidth(), jmax (400, tlc->getHeight() + delta));
    else
        resized();
}

void MainComponent::toggleKeyboard()
{
    keyboardVisible = ! keyboardVisible;
    kbdToggleButton->setToggleState (keyboardVisible, dontSendNotification);
    int delta = (keyboardVisible ? 1 : -1) * (lastKeyboardHeight + 8);
    if (auto* tlc = getTopLevelComponent())
        tlc->setSize (tlc->getWidth(), jmax (400, tlc->getHeight() + delta));
    else
        resized();
}

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

void MainComponent::setPluginName (const String& name, int id) { pluginButton->setButtonText (name); selectedPluginId = id; }
int  MainComponent::getSelectedPluginID() const                { return selectedPluginId; }
void MainComponent::setPresetButtonEnabled (bool e)            { presetButton->setEnabled (e); }
void MainComponent::setPluginLoaded (bool loaded)              { unloadButton->setEnabled (loaded); }
void MainComponent::setMidiMonitorText (const String& t)       { midiMonitorLabel->setText (t, dontSendNotification); }

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
    int delta = (v ? 1 : -1) * (lastMonitorHeight + 8);
    if (auto* tlc = getTopLevelComponent())
        tlc->setSize (tlc->getWidth(), jmax (400, tlc->getHeight() + delta));
    else
        resized();
}

void MainComponent::paint (Graphics& g)
{
    g.fillAll (Colour (0xff14141f));
    g.setColour (Colour (0xff252535));
    g.fillRect (getLocalBounds().removeFromTop (44));
}

void MainComponent::resized()
{
    auto area        = getLocalBounds();
    auto toolbarArea = area.removeFromTop (44);

    lvhLogo->setBounds (toolbarArea.removeFromLeft (60));
    auto toolbar = toolbarArea.reduced (2, 4);

    // Left section: plugin selector + action buttons
    pluginButton->setBounds        (toolbar.removeFromLeft (195).reduced (2));
    presetButton->setBounds        (toolbar.removeFromLeft (36).reduced (2));
    toolbar.removeFromLeft (4);
    loadButton->setBounds          (toolbar.removeFromLeft (36).reduced (2));
    unloadButton->setBounds        (toolbar.removeFromLeft (36).reduced (2));
    panicButton->setBounds         (toolbar.removeFromLeft (36).reduced (2));
    kbdToggleButton->setBounds     (toolbar.removeFromLeft (36).reduced (2));
    monitorToggleButton->setBounds (toolbar.removeFromLeft (36).reduced (2));

    // Right section (monitoring) — pulled from the right; conditional on visibility
    if (showMidiMonitor)  midiMonitorLabel->setBounds (toolbar.removeFromRight (160).reduced (2));
    if (showLevelMeter)   levelMeter->setBounds       (toolbar.removeFromRight (110).reduced (2, 4));
    volumeSlider->setBounds  (toolbar.removeFromRight (80).reduced (2));
    speakerButton->setBounds (toolbar.removeFromRight (36).reduced (2));

    // Content area — 5-slot layout; hidden slots get 0px
    auto content = area.reduced (4);

    stretchLayout.setItemLayout (1,
        monitorVisible ? 8 : 0,
        monitorVisible ? 8 : 0,
        monitorVisible ? 8 : 0);
    stretchLayout.setItemLayout (2,
        monitorVisible ? 60  : 0,
        monitorVisible ? 400 : 0,
        monitorVisible ? lastMonitorHeight : 0);
    stretchLayout.setItemLayout (3,
        keyboardVisible ? 8 : 0,
        keyboardVisible ? 8 : 0,
        keyboardVisible ? 8 : 0);
    stretchLayout.setItemLayout (4,
        keyboardVisible ? 50  : 0,
        keyboardVisible ? 300 : 0,
        keyboardVisible ? lastKeyboardHeight : 0);

    Component* comps[] = {
        pluginViewport.get(), resizerBar.get(),
        monitorPanel.get(),   resizerBar2.get(),
        keyboardComponent.get()
    };
    stretchLayout.layOutComponents (comps, 5,
        content.getX(), content.getY(), content.getWidth(), content.getHeight(), true, true);

    if (monitorVisible)  lastMonitorHeight  = monitorPanel->getHeight();
    if (keyboardVisible) lastKeyboardHeight = keyboardComponent->getHeight();

    resizerBar ->setVisible (monitorVisible);
    resizerBar2->setVisible (keyboardVisible);
    monitorPanel->setVisible (monitorVisible);
    keyboardComponent->setVisible (keyboardVisible);

    scanOverlay->setBounds (getLocalBounds());
}
