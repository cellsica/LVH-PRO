#pragma once
#include "UiCommon.h"
#include "UiComponents.h"
#include "LevelMeter.h"
#include "InfoMonitorPanel.h"

// =====================================================================
// MainComponent
// =====================================================================
class MainComponent : public Component
{
private:
    std::unique_ptr<LvhLogoLabel>                lvhLogo;
    std::unique_ptr<PcKeyboardComponent>         keyboardComponent;
    std::unique_ptr<TextButton>                  pluginButton;
    std::unique_ptr<IconButton>                  presetButton;
    std::unique_ptr<IconButton>                  loadButton;
    std::unique_ptr<IconButton>                  unloadButton;
    std::unique_ptr<IconButton>                  panicButton;
    std::unique_ptr<IconButton>                  kbdToggleButton;
    std::unique_ptr<SpeakerButton>               speakerButton;
    std::unique_ptr<Slider>                      volumeSlider;
    std::unique_ptr<LevelMeter>                  levelMeter;
    std::unique_ptr<Label>                       midiMonitorLabel;
    std::unique_ptr<InfoMonitorPanel>            monitorPanel;
    std::unique_ptr<IconButton>                  monitorToggleButton;
    std::unique_ptr<Viewport>                    pluginViewport;
    StretchableLayoutManager                     stretchLayout;
    std::unique_ptr<StretchableLayoutResizerBar> resizerBar;
    std::unique_ptr<StretchableLayoutResizerBar> resizerBar2;
    std::unique_ptr<ScanOverlay>                 scanOverlay;

    String currentPluginName  = "Select Plugin...";
    int    selectedPluginId   = 0;
    bool   keyboardVisible    = true;
    bool   monitorVisible     = true;
    bool   showLevelMeter     = true;
    bool   showMidiMonitor    = true;
    int    lastKeyboardHeight = 50;
    int    lastMonitorHeight  = 120;

public:
    explicit MainComponent (MidiKeyboardState& state);

    void setPluginEditor (AudioProcessorEditor* editor);
    void resizeToFitEditor (AudioProcessorEditor* editor);
    void toggleMonitor();
    void toggleKeyboard();

    void showScanOverlay();
    void hideScanOverlay();
    void updateScanProgress (const String& f);

    void setPluginName (const String& name, int id);
    int  getSelectedPluginID() const;
    void setPresetButtonEnabled (bool e);
    void setPluginLoaded (bool loaded);
    void setMidiMonitorText (const String& t);

    Slider&           getVolumeSlider();
    LevelMeter&       getLevelMeter();
    SpeakerButton&    getSpeakerButton();
    InfoMonitorPanel& getMonitorPanel();

    void setLevelMeterVisible (bool v);
    void setMidiMonitorVisible (bool v);
    void setMonitorPanelVisible (bool v);

    std::function<void()> onLogoRightClick;
    std::function<void()> onPluginMenuRequest;
    std::function<void()> onPresetMenuRequest;
    std::function<void()> onLoadClicked;
    std::function<void()> onUnloadClicked;
    std::function<void()> onPanicClicked;
    std::function<void()> onLaunchBridgeClicked;

    void paint (Graphics& g) override;
    void resized() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
