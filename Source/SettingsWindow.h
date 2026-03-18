#pragma once
#include "UiCommon.h"

// =====================================================================
// Settings Pages
// =====================================================================
class GeneralSettingsPage : public Component
{
public:
    std::function<void(bool)> onShowLevelMeter, onShowMidiMonitor, onShowInfoMonitor;

    explicit GeneralSettingsPage (PropertiesFile* prefs);
    void resized() override;

private:
    ToggleButton showLevelMeter, showMidiMonitor, showInfoMonitor;
    ToggleButton rememberLastFolder;
    Label        recentCountLabel;
    Slider       recentCountSlider;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeneralSettingsPage)
};

class AudioMidiSettingsPage : public Component
{
public:
    explicit AudioMidiSettingsPage (AudioDeviceManager& dm);
    void resized() override;

private:
    std::unique_ptr<AudioDeviceSelectorComponent> selector;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioMidiSettingsPage)
};

class PluginPathsPage : public Component
{
public:
    PluginPathsPage();
    void resized() override;

private:
    TextEditor info;
    TextButton addBtn, rmBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPathsPage)
};

class MidiSettingsPage : public Component
{
public:
    std::function<void(int)> onTransposeChange, onChannelFilterChange;

    explicit MidiSettingsPage (PropertiesFile* prefs);
    void resized() override;

private:
    Label    tpHeader, chHeader;
    Slider   tpSlider;
    ComboBox chCombo;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiSettingsPage)
};

class PluginInfoPage : public Component
{
public:
    PluginInfoPage();
    void update (const String& name, int latency, const String& format, int ins, int outs);
    void resized() override;

private:
    Label nameLbl, latencyLbl, formatLbl, ioLbl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginInfoPage)
};

// =====================================================================
// SettingsWindow — flat left-nav + right content pane
// =====================================================================
class SettingsWindow : public DocumentWindow
{
public:
    struct Callbacks
    {
        std::function<void(bool)> onShowLevelMeter, onShowMidiMonitor, onShowInfoMonitor;
        std::function<void(int)>  onTransposeChange, onChannelFilterChange;
    };

    SettingsWindow (AudioDeviceManager& dm, PropertiesFile* prefs, Callbacks cbs);
    void updatePluginInfo (const String& name, int latency, const String& fmt, int ins, int outs);
    void closeButtonPressed() override;

private:
    class Content : public Component
    {
    public:
        static constexpr int navW = 148;
        static constexpr int rowH = 34;

        Content (AudioDeviceManager& dm, PropertiesFile* prefs, Callbacks& cbs);
        void updatePluginInfo (const String& name, int latency, const String& fmt, int ins, int outs);
        void paint (Graphics& g) override;
        void mouseDown (const MouseEvent& e) override;
        void resized() override;

    private:
        void addPage (const String& name, Component* page);
        void selectPage (int idx);

        StringArray           names;
        OwnedArray<Component> pages;
        int                   currentIdx = -1;
        PluginInfoPage*       infoPage   = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

    std::unique_ptr<Content> content; // transferred to DocumentWindow ownership

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};
