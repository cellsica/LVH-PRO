#include "SystemLogPanel.h"
#include "Core/ThemePalette.h"

SystemLogPanel::SystemLogPanel()
{
    log.setMultiLine (true);
    log.setReadOnly (true);
    log.setScrollbarsShown (true);
    log.setFont (Font (Font::getDefaultMonospacedFontName(), 10.5f, Font::plain));
    log.setColour (TextEditor::backgroundColourId, ThemePalette::get (ColourId::BgLog));
    log.setColour (TextEditor::textColourId,       ThemePalette::get (ColourId::TextSysLog));
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
    g.fillAll (ThemePalette::get (ColourId::BgLog));
    g.setColour (ThemePalette::get (ColourId::BgPanelAlt));
    g.fillRect (getLocalBounds().removeFromTop (20));
    g.setColour (ThemePalette::get (ColourId::TextSysLog));
    g.setFont (Font (11.0f));
    g.drawText ("Messages", getLocalBounds().removeFromTop (20).reduced (6, 0),
                Justification::centredLeft);
}

void SystemLogPanel::resized()
{
    log.setBounds (getLocalBounds().withTrimmedTop (20).reduced (0, 1));
}
