#include "SystemLogPanel.h"

SystemLogPanel::SystemLogPanel()
{
    log.setMultiLine (true);
    log.setReadOnly (true);
    log.setScrollbarsShown (true);
    log.setFont (Font (Font::getDefaultMonospacedFontName(), 10.5f, Font::plain));
    log.setColour (TextEditor::backgroundColourId, Colour (0xff0e0e18));
    log.setColour (TextEditor::textColourId,       Colour (0xff99dd99));
    log.setColour (TextEditor::outlineColourId,    Colours::transparentBlack);
    addAndMakeVisible (log);
}

void SystemLogPanel::pushMessage (const String& text)
{
    // May be called from any thread — defer to message thread.
    MessageManager::callAsync ([this, text] { appendLine (text); });
}

void SystemLogPanel::appendLine (const String& text)
{
    auto t = Time::getCurrentTime();
    String timestamp = String::formatted ("%02d:%02d:%02d",
                                          t.getHours(), t.getMinutes(), t.getSeconds());
    lines.add ("[" + timestamp + "] " + text);
    while (lines.size() > maxLines) lines.remove (0);
    log.setText (lines.joinIntoString ("\n"), dontSendNotification);
    log.moveCaretToEnd();
}

void SystemLogPanel::paint (Graphics& g)
{
    g.fillAll (Colour (0xff0e0e18));
    g.setColour (Colour (0xff252535));
    g.fillRect (getLocalBounds().removeFromTop (20));
    g.setColour (Colour (0xff99dd99));
    g.setFont (Font (11.0f));
    g.drawText ("Messages", getLocalBounds().removeFromTop (20).reduced (6, 0),
                Justification::centredLeft);
}

void SystemLogPanel::resized()
{
    log.setBounds (getLocalBounds().withTrimmedTop (20).reduced (0, 1));
}
