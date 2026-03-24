#pragma once
#include "UiCommon.h"

// =====================================================================
// Settings Pages
// =====================================================================
class GeneralSettingsPage : public Component
{
public:
    std::function<void(bool)>          onShowLevelMeter, onShowMidiMonitor, onShowInfoMonitor;
    std::function<void(juce::String)>  onLanguageChanged;

    explicit GeneralSettingsPage (PropertiesFile* prefs);
    void resized() override;
    void refreshLanguage();

private:
    ToggleButton showLevelMeter, showMidiMonitor, showInfoMonitor;
    ToggleButton rememberLastFolder;
    Label        recentCountLabel;
    Slider       recentCountSlider;
    Label        languageLabel_;
    ComboBox     languageCombo_;
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

class PluginPathsPage : public Component, public ListBoxModel
{
public:
    explicit PluginPathsPage (PropertiesFile* prefs);
    void resized() override;
    void refreshLanguage();

    // ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem (int row, Graphics& g, int width, int height, bool selected) override;

    std::function<void()> onPathsChanged;

private:
    void loadPaths();
    void savePaths();

    PropertiesFile* prefs;
    StringArray     paths;
    ListBox         pathList { {}, this };
    TextButton      addBtn, rmBtn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPathsPage)
};

class MidiSettingsPage : public Component
{
public:
    std::function<void(int)> onTransposeChange, onChannelFilterChange;

    explicit MidiSettingsPage (PropertiesFile* prefs);
    void resized() override;
    void refreshLanguage();

private:
    // ── Transpose / Channel filter ────────────────────────────────────────
    Label    tpHeader, chHeader;
    Slider   tpSlider;
    ComboBox chCombo;

    // ── Stage Remote Control ──────────────────────────────────────────────
    Label    remoteHeader_, methodLabel_, remoteChanLabel_;
    Label    ccPrevLabel_, ccNextLabel_, ccLoadLabel_;
    ComboBox methodCombo_, remoteChanCombo_;
    Slider   ccPrevSlider_, ccNextSlider_, ccLoadSlider_;

    PropertiesFile* prefs_ = nullptr;   // kept for CC slider onChange

    void updateCCVisibility();          // show/hide CC sliders based on method

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiSettingsPage)
};

// =====================================================================
// SettingsWindow — flat left-nav + right content pane
// =====================================================================
class SettingsWindow : public DocumentWindow
{
public:
    struct Callbacks
    {
        std::function<void(bool)>         onShowLevelMeter, onShowMidiMonitor, onShowInfoMonitor;
        std::function<void(int)>          onTransposeChange, onChannelFilterChange;
        std::function<void()>             onPluginPathsChanged;
        std::function<void(juce::String)> onLanguageChanged;
    };

    SettingsWindow (AudioDeviceManager& dm, PropertiesFile* prefs, Callbacks cbs);
    void refresh();
    void closeButtonPressed() override;

private:
    class Content : public Component
    {
    public:
        static constexpr int navW = 148;
        static constexpr int rowH = 34;

        Content (AudioDeviceManager& dm, PropertiesFile* prefs, Callbacks& cbs);
        void refresh();
        void paint (Graphics& g) override;
        void mouseDown (const MouseEvent& e) override;
        void resized() override;

    private:
        void addPage (const String& name, Component* page);
        void selectPage (int idx);

        StringArray           names;
        OwnedArray<Component> pages;
        int                   currentIdx    = -1;
        GeneralSettingsPage*  genPage_      = nullptr;
        MidiSettingsPage*     midiPage_     = nullptr;
        PluginPathsPage*      pathsPage_    = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

    std::unique_ptr<Content> content; // transferred to DocumentWindow ownership

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};
