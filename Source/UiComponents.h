#pragma once
#include "UiCommon.h"

// =====================================================================
// SpeakerButton — mute toggle with speaker/mute icon
// =====================================================================
class SpeakerButton : public Button
{
public:
    SpeakerButton();
    void paintButton (Graphics& g, bool highlighted, bool down) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpeakerButton)
};

// =====================================================================
// IconButton
// =====================================================================
class IconButton : public Button
{
public:
    using DrawFn = std::function<void(Graphics&, Rectangle<float>)>;
    IconButton (const String& tooltip, DrawFn fn);
    void paintButton (Graphics& g, bool highlighted, bool down) override;

private:
    DrawFn draw;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};

// =====================================================================
// PcKeyboardComponent — MidiKeyboardComponent with PC-key overlay labels
// =====================================================================
class PcKeyboardComponent : public MidiKeyboardComponent
{
public:
    PcKeyboardComponent (MidiKeyboardState& state);

    void setOctaveOffset (int offset);

    bool keyPressed (const KeyPress&) override;
    bool keyStateChanged (bool) override;

    void drawWhiteNote (int midiNoteNumber, Graphics& g, Rectangle<float> area,
                        bool isDown, bool isOver, Colour lineColour, Colour textColour) override;

    void drawBlackNote (int midiNoteNumber, Graphics& g, Rectangle<float> area,
                        bool isDown, bool isOver, Colour noteFillColour) override;

private:
    int octaveOffset = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PcKeyboardComponent)
};

// =====================================================================
// LvhLogoLabel — right-click opens Settings / MIDI Input menu
// =====================================================================
class LvhLogoLabel : public Component
{
public:
    LvhLogoLabel();
    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;

    std::function<void()> onRightClick;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LvhLogoLabel)
};

// =====================================================================
// PCKeyboardListener — intercepts window-level key events for MIDI
// =====================================================================
class PCKeyboardListener : public KeyListener
{
public:
    explicit PCKeyboardListener (MidiKeyboardState& state);

    void setOctaveOffset (int offset) { octaveOffset = offset; }

    // Called with +1 or -1 when [ or ] is pressed
    std::function<void(int)> onOctaveShift;

    bool keyPressed (const KeyPress& key, Component*) override;
    bool keyStateChanged (bool, Component*) override;

private:
    MidiKeyboardState& keyboardState;
    std::map<int, int> heldNotes;  // keyCode → MIDI note actually sent
    int octaveOffset = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PCKeyboardListener)
};

// =====================================================================
// ScanOverlay
// =====================================================================
class ScanOverlay : public Component
{
public:
    ScanOverlay();
    void setProgress (const String& filename);
    void paint (Graphics& g) override;

private:
    Image logo;
    String currentFile;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScanOverlay)
};
