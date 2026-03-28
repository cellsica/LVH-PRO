#pragma once
#include "UiCommon.h"
#include "UiComponents.h"
#include "LevelMeter.h"
#include "InfoMonitorPanel.h"
#include "SystemLogPanel.h"
#include "MixerWindow.h"


// =====================================================================
// MainComponent
//
// Layout (top → bottom):
//   Toolbar (fixed 44px)
//   SystemLogPanel       — application message log  ← absorbs all size changes
//   [HResizer]           — top bar: changes MIDILog height, keyboard fixed
//   InfoMonitorPanel     — MIDI message log + status
//   [HResizer]           — bottom bar: changes keyboard height, MIDILog fixed
//   PcKeyboardComponent  — virtual keyboard
// =====================================================================
class MainComponent : public Component
{
private:
    // ── Toolbar ───────────────────────────────────────────────────────
    std::unique_ptr<LvhLogoLabel>     lvhLogo;
    std::unique_ptr<IconButton>       panicButton;
    std::unique_ptr<IconButton>       kbdToggleButton;
    std::unique_ptr<IconButton>       monitorToggleButton;
    std::unique_ptr<IconButton>       mixerToggleButton;
    std::unique_ptr<IconButton>       stageToggleButton;
    std::unique_ptr<IconButton>       metronomeToggleButton;
    std::unique_ptr<IconButton>       vuMeterToggleButton;

    std::unique_ptr<TextButton>       octaveDownButton;
    std::unique_ptr<Label>            octaveLabel;
    std::unique_ptr<TextButton>       octaveUpButton;
    std::unique_ptr<SpeakerButton>    speakerButton;
    std::unique_ptr<Slider>           volumeSlider;
    std::unique_ptr<Label>            volumeValueLabel;
    std::unique_ptr<LevelMeter>       levelMeter;
    std::unique_ptr<Label>            midiMonitorLabel;

    // ── Content panels ────────────────────────────────────────────────
    std::unique_ptr<SystemLogPanel>   systemLogPanel;
    std::unique_ptr<InfoMonitorPanel> monitorPanel;
    std::unique_ptr<PcKeyboardComponent> keyboardComponent;

    // ── Custom resizer bars ───────────────────────────────────────────
    // Simple horizontal drag bar.
    //   onDrag(delta): called with pixel delta on each mouse-drag step.
    struct HResizer : public Component
    {
        std::function<void(int)> onDrag;

        void mouseEnter (const MouseEvent&) override
            { setMouseCursor (MouseCursor::UpDownResizeCursor); }
        void mouseExit  (const MouseEvent&) override
            { setMouseCursor (MouseCursor::NormalCursor); }
        void mouseDown  (const MouseEvent&) override { lastY = 0; }
        void mouseDrag  (const MouseEvent& e) override
        {
            int d = e.getDistanceFromDragStartY() - lastY;
            lastY = e.getDistanceFromDragStartY();
            if (onDrag && d != 0) onDrag (d);
        }
        void paint (Graphics& g) override
        {
            g.fillAll (Colour (0xff252535));
            g.setColour (Colour (0xff555566));
            auto cx = getWidth() / 2;
            auto cy = getHeight() / 2;
            for (int x = cx - 20; x <= cx + 20; x += 8)
                g.fillEllipse ((float)(x - 1), (float)(cy - 1), 3.0f, 3.0f);
        }
    private:
        int lastY = 0;
    };

    std::unique_ptr<HResizer> resizerBar;   // top:    changes monitorHeight
    std::unique_ptr<HResizer> resizerBar2;  // bottom: changes keyboardHeight

    // ── Overlay ───────────────────────────────────────────────────────
    std::unique_ptr<ScanOverlay> scanOverlay;

    // ── Tooltip ───────────────────────────────────────────────────────
    TooltipWindow tooltipWindow { this, 500 };  // 500ms delay

    // ── Layout state ──────────────────────────────────────────────────
    bool keyboardVisible    = true;
    bool monitorVisible     = true;
    bool showLevelMeter     = true;
    bool showMidiMonitor    = true;
    int  lastKeyboardHeight = 80;
    int  lastMonitorHeight  = 150;

    static constexpr int kResizerH = 8;
    static constexpr int kMinLogH  = 40;
    static constexpr int kMinMidiH = 40;
    static constexpr int kMinKbdH  = 50;

public:
    explicit MainComponent (MidiKeyboardState& state);

    void toggleMonitor();
    void toggleKeyboard();
    void toggleMixer();
    void toggleStage();
    void toggleMetronome();
    void toggleVuMeter();

    void showScanOverlay();
    void hideScanOverlay();
    void updateScanProgress (const String& f);

    void setMidiMonitorText (const String& t);
    void pushSystemMessage  (const String& text);

    Slider&           getVolumeSlider();
    void              setVolumeDisplay (double v);
    LevelMeter&       getLevelMeter();
    SpeakerButton&    getSpeakerButton();
    InfoMonitorPanel& getMonitorPanel();

    void setLevelMeterVisible   (bool v);
    void setMidiMonitorVisible  (bool v);
    void setMonitorPanelVisible (bool v);
    void setMixerWindowVisible      (bool v);
    void setStageWindowVisible      (bool v);
    void setMetronomeWindowVisible  (bool v);
    void setVuMeterWindowVisible    (bool v);
    void setOctaveDisplay       (int octaveNumber);


    PcKeyboardComponent& getKeyboardComponent();

    std::function<void()>  onLogoRightClick;
    std::function<void()>  onPanicClicked;
    std::function<void()>  onLaunchBridgeClicked;
    std::function<void(bool)> onMixerToggle;
    std::function<void(bool)> onStageToggle;
    std::function<void(bool)> onMetronomeToggle;
    std::function<void(bool)> onVuMeterToggle;
    std::function<void(int)> onOctaveShift;  // called with +1 or -1
    std::function<void(double)> onVolumeChanged;


    void paint   (Graphics& g) override;
    void resized ()             override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
