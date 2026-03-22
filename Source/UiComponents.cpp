#include "UiComponents.h"
#include <BinaryData.h>

// =====================================================================
// SpeakerButton
// =====================================================================
SpeakerButton::SpeakerButton() : Button ("Speaker / Mute")
{
    setClickingTogglesState (true);
    setTooltip ("Mute / Unmute");
}

void SpeakerButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    bool muted = getToggleState();
    auto bounds = getLocalBounds().toFloat();
    Colour bg = muted ? Colour (0xff881111)
                      : findColour (TextButton::buttonColourId);
    if (down)        bg = bg.brighter (0.4f);
    else if (highlighted) bg = bg.brighter (0.15f);
    g.setColour (bg);
    g.fillRoundedRectangle (bounds.reduced (1.f), 4.f);

    g.setFont (Font ("Segoe UI Emoji", 18.0f, Font::plain));
    g.setColour (Colours::white);
    g.drawText (muted ? String::fromUTF8 ("\xF0\x9F\x94\x87")   // 🔇
                      : String::fromUTF8 ("\xF0\x9F\x94\x8A"),  // 🔊
                bounds.toNearestInt(), Justification::centred, false);
}

// =====================================================================
// IconButton
// =====================================================================
IconButton::IconButton (const String& tooltip, DrawFn fn) : Button (tooltip), draw (std::move (fn))
{
    setTooltip (tooltip);
}

void IconButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat();
    Colour bg = getToggleState() ? findColour (TextButton::buttonOnColourId)
                                 : findColour (TextButton::buttonColourId);
    if (! isEnabled())    bg = bg.withAlpha (0.35f);
    else if (down)        bg = bg.brighter (0.4f);
    else if (highlighted) bg = bg.brighter (0.15f);
    g.setColour (bg);
    g.fillRoundedRectangle (bounds.reduced (1.f), 4.f);
    g.setOpacity (isEnabled() ? 1.f : 0.4f);
    draw (g, bounds.reduced (5.f));
    g.setOpacity (1.f);
}

// =====================================================================
// PcKeyboardComponent
// =====================================================================
PcKeyboardComponent::PcKeyboardComponent (MidiKeyboardState& state)
    : MidiKeyboardComponent (state, MidiKeyboardComponent::horizontalKeyboard)
{
    setOctaveForMiddleC (4);    // standard convention: MIDI 60 = C4, MIDI 72 = C5
    setLowestVisibleKey (60);   // start view at C4 so Z (C5) is visible
}

void PcKeyboardComponent::setOctaveOffset (int offset)
{
    octaveOffset = offset;
    setLowestVisibleKey (jlimit (0, 107, 60 + offset * 12));
    repaint();
}

bool PcKeyboardComponent::keyPressed (const KeyPress&) { return false; }
bool PcKeyboardComponent::keyStateChanged (bool)       { return false; }

void PcKeyboardComponent::drawWhiteNote (int midiNoteNumber, Graphics& g, Rectangle<float> area,
                                         bool isDown, bool isOver, Colour lineColour, Colour textColour)
{
    MidiKeyboardComponent::drawWhiteNote (midiNoteNumber, g, area, isDown, isOver, lineColour, textColour);
    auto it = noteKeyLabels().find (midiNoteNumber - octaveOffset * 12);
    if (it != noteKeyLabels().end())
    {
        g.setColour (Colour (0xaa000000));
        g.setFont (Font (8.5f, Font::bold));
        g.drawText (it->second, area.withTrimmedTop (area.getHeight() * 0.72f).withHeight (10.f),
                    Justification::centred);
    }
}

void PcKeyboardComponent::drawBlackNote (int midiNoteNumber, Graphics& g, Rectangle<float> area,
                                         bool isDown, bool isOver, Colour noteFillColour)
{
    MidiKeyboardComponent::drawBlackNote (midiNoteNumber, g, area, isDown, isOver, noteFillColour);
    auto it = noteKeyLabels().find (midiNoteNumber - octaveOffset * 12);
    if (it != noteKeyLabels().end())
    {
        g.setColour (Colours::white.withAlpha (0.85f));
        g.setFont (Font (7.5f, Font::bold));
        g.drawText (it->second, area.withTrimmedTop (area.getHeight() * 0.65f).withHeight (9.f),
                    Justification::centred);
    }
}

// =====================================================================
// LvhLogoLabel
// =====================================================================
LvhLogoLabel::LvhLogoLabel()
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void LvhLogoLabel::paint (Graphics& g)
{
    g.setColour (Colours::white);
    g.setFont (Font (18.0f, Font::bold));
    g.drawText ("LVH", getLocalBounds().reduced (8, 0), Justification::centredLeft);
}

void LvhLogoLabel::mouseDown (const MouseEvent& e)
{
    if (e.mods.isRightButtonDown() && onRightClick)
        onRightClick();
}

// =====================================================================
// PCKeyboardListener
// =====================================================================
PCKeyboardListener::PCKeyboardListener (MidiKeyboardState& state) : keyboardState (state) {}

bool PCKeyboardListener::keyPressed (const KeyPress& key, Component*)
{
    int kc = key.getKeyCode();

    // Octave shift shortcuts
    if (kc == '[') { if (onOctaveShift) onOctaveShift (-1); return true; }
    if (kc == ']') { if (onOctaveShift) onOctaveShift (+1); return true; }

    int baseNote = pcKeyToNote (kc);
    if (baseNote >= 0 && heldNotes.find (kc) == heldNotes.end())
    {
        int note = jlimit (0, 127, baseNote + octaveOffset * 12);
        heldNotes[kc] = note;
        keyboardState.noteOn (1, note, 0.8f);
        return true;
    }
    return false;
}

bool PCKeyboardListener::keyStateChanged (bool, Component*)
{
    for (auto it = heldNotes.begin(); it != heldNotes.end(); )
    {
        if (! KeyPress::isKeyCurrentlyDown (it->first))
        {
            keyboardState.noteOff (1, it->second, 0.0f);
            it = heldNotes.erase (it);
        }
        else ++it;
    }
    return false;
}

// =====================================================================
// ScanOverlay
// =====================================================================
ScanOverlay::ScanOverlay()
{
    setInterceptsMouseClicks (true, true);
    setVisible (false);
    logo = ImageCache::getFromMemory (BinaryData::LVH_LOGO_png, BinaryData::LVH_LOGO_pngSize);
}

void ScanOverlay::setProgress (const String& filename)
{
    currentFile = filename;
    repaint();
}

void ScanOverlay::paint (Graphics& g)
{
    g.fillAll (Colour (0xd8101018));
    auto bounds = getLocalBounds().toFloat();
    float cy = bounds.getCentreY();

    if (logo.isValid())
    {
        const int logoSize = 96;
        Rectangle<float> logoArea ((bounds.getWidth() - logoSize) * 0.5f, cy - 110.f,
                                   (float) logoSize, (float) logoSize);
        g.drawImageWithin (logo, (int) logoArea.getX(), (int) logoArea.getY(),
                           (int) logoArea.getWidth(), (int) logoArea.getHeight(),
                           RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize);
    }

    g.setColour (Colours::white);
    g.setFont (Font (20.0f, Font::bold));
    g.drawText ("Scanning Plugins...", bounds.withY (cy - 4).withHeight (32), Justification::centred);
    g.setColour (Colour (0xffaaaaaa));
    g.setFont (Font (12.0f));
    g.drawText (currentFile, bounds.withY (cy + 30).withHeight (20), Justification::centred, true);
}
