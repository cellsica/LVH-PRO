#include "InfoMonitorPanel.h"

InfoMonitorPanel::InfoMonitorPanel()
{
    midiLog.setMultiLine (true);
    midiLog.setReadOnly (true);
    midiLog.setScrollbarsShown (true);
    midiLog.setFont (Font (Font::getDefaultMonospacedFontName(), 10.5f, Font::plain));
    midiLog.setColour (TextEditor::backgroundColourId, Colour (0xff0e0e18));
    midiLog.setColour (TextEditor::textColourId,       Colour (0xff88ccff));
    midiLog.setColour (TextEditor::outlineColourId,    Colours::transparentBlack);
    addAndMakeVisible (midiLog);

    auto initLabel = [&] (Label& l, const String& text) {
        l.setFont (Font (10.5f));
        l.setColour (Label::textColourId, Colour (0xffaaaaaa));
        l.setText (text, dontSendNotification);
        addAndMakeVisible (l);
    };
    initLabel (channelLabel,   "Ch: --");
    initLabel (transposeLabel, "Xpose: 0");
    initLabel (sustainLabel,   "SUS: OFF");
    initLabel (cpuLabel,       "DSP: --%");

    startTimerHz (10);
}

void InfoMonitorPanel::pushMidiMessage (const MidiMessage& m)
{
    String text;
    if (m.isNoteOn())
    {
        text = "NoteOn  " + MidiMessage::getMidiNoteName (m.getNoteNumber(), true, true, 4)
             + "  Vel:" + String (m.getVelocity())
             + "  Ch:"  + String (m.getChannel());
        pendingChannel = m.getChannel();
    }
    else if (m.isNoteOff())
    {
        text = "NoteOff " + MidiMessage::getMidiNoteName (m.getNoteNumber(), true, true, 4)
             + "  Ch:" + String (m.getChannel());
    }
    else if (m.isController())
    {
        text = "CC" + String (m.getControllerNumber())
             + "=" + String (m.getControllerValue())
             + "  Ch:" + String (m.getChannel());
        if (m.getControllerNumber() == 64)
            sustainPending = (m.getControllerValue() >= 64);
    }
    else if (m.isPitchWheel())
    {
        text = "Pitch=" + String (m.getPitchWheelValue())
             + "  Ch:" + String (m.getChannel());
    }
    else return;

    // pushMidiMessage is always called on the message thread (via callAsync in Main.cpp).
    // Call rebuildLog() directly — no need for a second callAsync hop.
    logLines.add (text);
    while (logLines.size() > maxLogLines) logLines.remove (0);
    rebuildLog();
}

void InfoMonitorPanel::setTransposeDisplay (int semitones)
{
    transposeLabel.setText ("Xpose: " + String (semitones >= 0 ? "+" : "") + String (semitones),
                            dontSendNotification);
}

void InfoMonitorPanel::paint (Graphics& g)
{
    g.fillAll (Colour (0xff0e0e18));
    g.setColour (Colour (0xff252535));
    g.fillRect (getLocalBounds().removeFromTop (22));
}

void InfoMonitorPanel::resized()
{
    auto area = getLocalBounds();
    auto statusBar = area.removeFromTop (22).reduced (4, 2);
    channelLabel  .setBounds (statusBar.removeFromLeft (56));
    transposeLabel.setBounds (statusBar.removeFromLeft (80));
    sustainLabel  .setBounds (statusBar.removeFromLeft (74));
    cpuLabel      .setBounds (statusBar.removeFromLeft (72));
    midiLog.setBounds (area.reduced (0, 1));
}

void InfoMonitorPanel::timerCallback()
{
    if (getCpuUsage)
        cpuLabel.setText ("DSP: " + String (roundToInt (getCpuUsage() * 100.0)) + "%",
                          dontSendNotification);

    bool s = sustainPending.load();
    sustainLabel.setColour (Label::textColourId, s ? Colour (0xff88ff88) : Colour (0xffaaaaaa));
    sustainLabel.setText (s ? "SUS: ON" : "SUS: OFF", dontSendNotification);

    int ch = pendingChannel.load();
    if (ch > 0) channelLabel.setText ("Ch: " + String (ch), dontSendNotification);
}

void InfoMonitorPanel::rebuildLog()
{
    midiLog.setText (logLines.joinIntoString ("\n"), dontSendNotification);
    midiLog.moveCaretToEnd();
}
