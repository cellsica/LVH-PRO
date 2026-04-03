#include "LevelMeter.h"
#include "Core/ThemePalette.h"

LevelMeter::LevelMeter()
{
    startTimerHz (30);
}

void LevelMeter::paint (Graphics& g)
{
    auto area    = getLocalBounds();
    const int labelW = 12;
    int h = area.getHeight() / 2 - 1;

    g.setFont (Font (9.0f, Font::bold));

    auto lRow = area.removeFromTop (h);
    g.setColour (displayL > 0.001f ? ThemePalette::get (ColourId::TextPrimary) : ThemePalette::get (ColourId::TextInactive));
    g.drawText ("L", lRow.removeFromLeft (labelW), Justification::centred);
    paintBar (g, lRow, displayL);

    area.removeFromTop (2);

    auto rRow = area.removeFromTop (h);
    g.setColour (displayR > 0.001f ? ThemePalette::get (ColourId::TextPrimary) : ThemePalette::get (ColourId::TextInactive));
    g.drawText ("R", rRow.removeFromLeft (labelW), Justification::centred);
    paintBar (g, rRow, displayR);
}

void LevelMeter::timerCallback()
{
    if (getPeak)
    {
        displayL = jmax (getPeak (0), displayL * 0.88f);
        displayR = jmax (getPeak (1), displayR * 0.88f);
    }
    repaint();
}

void LevelMeter::paintBar (Graphics& g, Rectangle<int> area, float level)
{
    g.setColour (ThemePalette::get (ColourId::BgPanel));
    g.fillRect (area);
    int w = area.getWidth(), fillW = (int)(w * jmin (1.f, level));
    if (fillW <= 0) return;
    int greenEnd = (int)(w * 0.70f), yellowEnd = (int)(w * 0.90f);
    g.setColour (ThemePalette::get (ColourId::MeterGreen));
    g.fillRect (area.getX(), area.getY(), jmin (fillW, greenEnd), area.getHeight());
    if (fillW > greenEnd)
    {
        g.setColour (ThemePalette::get (ColourId::MeterYellow));
        g.fillRect (area.getX() + greenEnd, area.getY(),
                    jmin (fillW - greenEnd, yellowEnd - greenEnd), area.getHeight());
    }
    if (fillW > yellowEnd)
    {
        g.setColour (ThemePalette::get (ColourId::MeterRed));
        g.fillRect (area.getX() + yellowEnd, area.getY(), fillW - yellowEnd, area.getHeight());
    }
}
