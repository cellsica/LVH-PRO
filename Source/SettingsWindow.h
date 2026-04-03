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
    std::function<void(int)>           onMetronomeClickTypeChanged;  // 0=Normal, 1=Techno
    std::function<void(int)>           onThemeChanged;               // 0=Dark,   1=Light

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
    Label              themeLabel_;
    ComboBox           themeCombo_;
    juce::StringArray  themeIds_;   ///< filename stems parallel to themeCombo_ items
    Label        metroClickLabel_;
    ToggleButton metroNormalBtn_, metroTechnoBtn_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeneralSettingsPage)
};

class AudioMidiSettingsPage : public Component
{
public:
    explicit AudioMidiSettingsPage (AudioDeviceManager& dm);
    void resized() override;

private:
    Viewport                                      viewport_;
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
    TextButton      addBtn, rmBtn, rescanBtn;

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
// VisualizerSettingsPage
// =====================================================================
class VisualizerSettingsPage : public Component
{
public:
    // Fired when the VU meter backlight theme changes (0 = VintageWarm, 1 = OxygenNeon).
    std::function<void(int)>   onVuThemeChanged;
    // Fired when the VU meter opacity changes (20–100).
    std::function<void(int)>   onVuOpacityChanged;

    explicit VisualizerSettingsPage (PropertiesFile* prefs);
    void resized() override;
    void refreshLanguage();

private:
    Label        vuHeader_;
    Label        themeLabel_;
    ToggleButton warmBtn_, neonBtn_;
    Label        opacityLabel_;
    Slider       opacitySlider_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizerSettingsPage)
};

// =====================================================================
// DisabledPluginsPage
// =====================================================================
class DisabledPluginsPage : public Component, public ListBoxModel
{
public:
    std::function<StringArray()>          onGetDisabledPlugins;
    std::function<void(const String&)>    onRestorePlugin;

    DisabledPluginsPage();
    void resized() override;
    void refresh();

    // ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem (int row, Graphics& g, int width, int height, bool selected) override;
    Component* refreshComponentForRow (int row, bool isSelected, Component* existing) override;

private:
    Label     headerLabel_;
    ListBox   listBox_ { {}, this };
    StringArray items_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DisabledPluginsPage)
};

// =====================================================================
// SettingsWindow — flat left-nav + right content pane
// =====================================================================
class SettingsWindow : public DocumentWindow
{
public:
    struct Callbacks
    {
        std::function<void(bool)>              onShowLevelMeter, onShowMidiMonitor, onShowInfoMonitor;
        std::function<void(int)>               onTransposeChange, onChannelFilterChange;
        std::function<void()>                  onPluginPathsChanged;
        std::function<void(juce::String)>      onLanguageChanged;
        std::function<void(int)>               onMetronomeClickTypeChanged;
        std::function<void(int)>               onVuThemeChanged;
        std::function<void(int)>               onVuOpacityChanged;
        std::function<juce::StringArray()>     onGetDisabledPlugins;
        std::function<void(const juce::String&)> onRestorePlugin;
        std::function<void(int)>               onThemeChanged;  // 0=Dark, 1=Light
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
        GeneralSettingsPage*      genPage_      = nullptr;
        MidiSettingsPage*         midiPage_     = nullptr;
        PluginPathsPage*          pathsPage_    = nullptr;
        VisualizerSettingsPage*   visualPage_   = nullptr;
        DisabledPluginsPage*      pluginsPage_  = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

    std::unique_ptr<Content> content; // transferred to DocumentWindow ownership

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};
