#include "SettingsWindow.h"

// =====================================================================
// GeneralSettingsPage
// =====================================================================
GeneralSettingsPage::GeneralSettingsPage (PropertiesFile* prefs)
{
    auto setup = [&] (ToggleButton& btn, const String& text, const String& key, bool def) {
        btn.setButtonText (text);
        btn.setToggleState (prefs ? prefs->getBoolValue (key, def) : def, dontSendNotification);
        addAndMakeVisible (btn);
    };
    setup (showLevelMeter,  "Level Meter (toolbar)",          "showLevelMeter",  true);
    setup (showMidiMonitor, "MIDI Monitor (toolbar)",         "showMidiMonitor", true);
    setup (showInfoMonitor, "Info Monitor Panel (main area)", "showInfoMonitor", true);

    showLevelMeter.onClick = [this, prefs] {
        bool v = showLevelMeter.getToggleState();
        if (prefs) prefs->setValue ("showLevelMeter", v);
        if (onShowLevelMeter) onShowLevelMeter (v);
    };
    showMidiMonitor.onClick = [this, prefs] {
        bool v = showMidiMonitor.getToggleState();
        if (prefs) prefs->setValue ("showMidiMonitor", v);
        if (onShowMidiMonitor) onShowMidiMonitor (v);
    };
    showInfoMonitor.onClick = [this, prefs] {
        bool v = showInfoMonitor.getToggleState();
        if (prefs) prefs->setValue ("showInfoMonitor", v);
        if (onShowInfoMonitor) onShowInfoMonitor (v);
    };
}

void GeneralSettingsPage::resized()
{
    auto area = getLocalBounds().reduced (16, 12);
    Label header;
    for (auto* btn : { &showLevelMeter, &showMidiMonitor, &showInfoMonitor })
    {
        btn->setBounds (area.removeFromTop (28));
        area.removeFromTop (4);
    }
}

// =====================================================================
// AudioMidiSettingsPage
// =====================================================================
AudioMidiSettingsPage::AudioMidiSettingsPage (AudioDeviceManager& dm)
{
    selector = std::make_unique<AudioDeviceSelectorComponent>
        (dm, 0, 0, 2, 2, true, true, true, false);
    addAndMakeVisible (*selector);
}

void AudioMidiSettingsPage::resized()
{
    selector->setBounds (getLocalBounds());
}

// =====================================================================
// PluginPathsPage
// =====================================================================
PluginPathsPage::PluginPathsPage()
{
    info.setMultiLine (true); info.setReadOnly (true);
    info.setText ("(Plugin scan path configuration — coming soon)");
    info.setColour (TextEditor::backgroundColourId, Colour (0xff1a1a2a));
    info.setColour (TextEditor::textColourId,       Colour (0xffaaaaaa));
    addAndMakeVisible (info);
    addBtn.setButtonText ("Add Path...");   addAndMakeVisible (addBtn);
    rmBtn .setButtonText ("Remove");        addAndMakeVisible (rmBtn);
}

void PluginPathsPage::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto row  = area.removeFromBottom (32);
    addBtn.setBounds (row.removeFromLeft (110).reduced (2));
    rmBtn .setBounds (row.removeFromLeft (80) .reduced (2));
    info.setBounds (area.reduced (0, 4));
}

// =====================================================================
// MidiSettingsPage
// =====================================================================
MidiSettingsPage::MidiSettingsPage (PropertiesFile* prefs)
{
    tpHeader.setText ("Transpose (semitones):", dontSendNotification);
    tpHeader.setFont (Font (12.f));
    tpHeader.setColour (Label::textColourId, Colour (0xffcccccc));
    addAndMakeVisible (tpHeader);

    tpSlider.setRange (-24, 24, 1);
    tpSlider.setValue (prefs ? prefs->getIntValue ("transpose", 0) : 0, dontSendNotification);
    tpSlider.setTextBoxStyle (Slider::TextBoxRight, false, 44, 22);
    addAndMakeVisible (tpSlider);

    tpSlider.onValueChange = [this, prefs] {
        int v = (int) tpSlider.getValue();
        if (prefs) prefs->setValue ("transpose", v);
        if (onTransposeChange) onTransposeChange (v);
    };

    chHeader.setText ("MIDI Channel Filter:", dontSendNotification);
    chHeader.setFont (Font (12.f));
    chHeader.setColour (Label::textColourId, Colour (0xffcccccc));
    addAndMakeVisible (chHeader);

    chCombo.addItem ("All Channels", 1);
    for (int i = 1; i <= 16; ++i) chCombo.addItem ("Channel " + String (i), i + 1);
    chCombo.setSelectedId ((prefs ? prefs->getIntValue ("channelFilter", 0) : 0) + 1,
                           dontSendNotification);
    addAndMakeVisible (chCombo);

    chCombo.onChange = [this, prefs] {
        int v = chCombo.getSelectedId() - 1;
        if (prefs) prefs->setValue ("channelFilter", v);
        if (onChannelFilterChange) onChannelFilterChange (v);
    };
}

void MidiSettingsPage::resized()
{
    auto area = getLocalBounds().reduced (16, 12);
    tpHeader.setBounds (area.removeFromTop (22));
    tpSlider.setBounds (area.removeFromTop (32));
    area.removeFromTop (16);
    chHeader.setBounds (area.removeFromTop (22));
    chCombo .setBounds (area.removeFromTop (28).removeFromLeft (200));
}

// =====================================================================
// PluginInfoPage
// =====================================================================
PluginInfoPage::PluginInfoPage()
{
    for (auto* l : { &nameLbl, &latencyLbl, &formatLbl, &ioLbl })
    {
        l->setFont (Font (12.f));
        l->setColour (Label::textColourId, Colour (0xffcccccc));
        addAndMakeVisible (l);
    }
    update ("\xe2\x80\x94", 0, "\xe2\x80\x94", 0, 0);
}

void PluginInfoPage::update (const String& name, int latency, const String& format, int ins, int outs)
{
    nameLbl   .setText ("Plugin:   " + name,                          dontSendNotification);
    latencyLbl.setText ("Latency:  " + String (latency) + " samples", dontSendNotification);
    formatLbl .setText ("Format:   " + format,                        dontSendNotification);
    ioLbl     .setText ("I/O:      " + String (ins) + " in / " + String (outs) + " out",
                        dontSendNotification);
}

void PluginInfoPage::resized()
{
    auto area = getLocalBounds().reduced (16, 12);
    for (auto* l : { &nameLbl, &latencyLbl, &formatLbl, &ioLbl })
    {
        l->setBounds (area.removeFromTop (24));
        area.removeFromTop (6);
    }
}

// =====================================================================
// SettingsWindow
// =====================================================================
SettingsWindow::SettingsWindow (AudioDeviceManager& dm, PropertiesFile* prefs, Callbacks cbs)
    : DocumentWindow ("Settings \xe2\x80\x94 LIGHT-VST-HOST",
                      Colour (0xff14141f), DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    content = std::make_unique<Content> (dm, prefs, cbs);
    setContentOwned (content.release(), true);
    setResizable (true, false);
    setResizeLimits (500, 350, 1200, 900);
    centreWithSize (700, 450);
}

void SettingsWindow::updatePluginInfo (const String& name, int latency, const String& fmt, int ins, int outs)
{
    if (auto* c = dynamic_cast<Content*> (getContentComponent()))
        c->updatePluginInfo (name, latency, fmt, ins, outs);
}

void SettingsWindow::closeButtonPressed()
{
    setVisible (false);
}

// =====================================================================
// SettingsWindow::Content
// =====================================================================
SettingsWindow::Content::Content (AudioDeviceManager& dm, PropertiesFile* prefs, Callbacks& cbs)
{
    auto* genPage = new GeneralSettingsPage (prefs);
    genPage->onShowLevelMeter  = cbs.onShowLevelMeter;
    genPage->onShowMidiMonitor = cbs.onShowMidiMonitor;
    genPage->onShowInfoMonitor = cbs.onShowInfoMonitor;
    addPage ("General",      genPage);
    addPage ("Audio / MIDI", new AudioMidiSettingsPage (dm));
    addPage ("Plugin Paths", new PluginPathsPage());

    auto* midiPage = new MidiSettingsPage (prefs);
    midiPage->onTransposeChange     = cbs.onTransposeChange;
    midiPage->onChannelFilterChange = cbs.onChannelFilterChange;
    addPage ("MIDI Settings", midiPage);

    infoPage = new PluginInfoPage();
    addPage ("Info", infoPage);

    selectPage (0);
    setSize (700, 450);
}

void SettingsWindow::Content::updatePluginInfo (const String& name, int latency,
                                                const String& fmt, int ins, int outs)
{
    if (infoPage) infoPage->update (name, latency, fmt, ins, outs);
}

void SettingsWindow::Content::paint (Graphics& g)
{
    // Nav background
    g.setColour (Colour (0xff1a1a2a));
    g.fillRect (0, 0, navW, getHeight());

    // Divider
    g.setColour (Colour (0xff303048));
    g.drawVerticalLine (navW, 0.f, (float) getHeight());

    // Nav items
    for (int i = 0; i < names.size(); ++i)
    {
        Rectangle<int> row (0, i * rowH, navW, rowH);
        bool sel = (i == currentIdx);

        if (sel)
        {
            g.setColour (Colour (0xff253555));
            g.fillRect (row);
            g.setColour (Colour (0xff4488dd));
            g.fillRect (0, row.getY(), 3, rowH);
        }

        g.setColour (sel ? Colours::white : Colour (0xff999aaa));
        g.setFont (Font (12.5f, sel ? Font::bold : Font::plain));
        g.drawText (names[i], row.withTrimmedLeft (12), Justification::centredLeft);
    }
}

void SettingsWindow::Content::mouseDown (const MouseEvent& e)
{
    if (e.x < navW)
    {
        int idx = e.y / rowH;
        if (idx >= 0 && idx < pages.size())
            selectPage (idx);
    }
}

void SettingsWindow::Content::resized()
{
    auto pageArea = Rectangle<int> (navW + 1, 0,
                                    getWidth() - navW - 1, getHeight());
    for (auto* p : pages)
        p->setBounds (pageArea);
}

void SettingsWindow::Content::addPage (const String& name, Component* page)
{
    names.add (name);
    pages.add (page);
    addChildComponent (*page);
}

void SettingsWindow::Content::selectPage (int idx)
{
    if (currentIdx >= 0 && currentIdx < pages.size())
        pages[currentIdx]->setVisible (false);
    currentIdx = idx;
    if (idx >= 0 && idx < pages.size())
        pages[idx]->setVisible (true);
    repaint();
}
