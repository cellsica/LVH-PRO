#include "SettingsWindow.h"
#include "Core/ThemePalette.h"

// =====================================================================
// GeneralSettingsPage
// =====================================================================
GeneralSettingsPage::GeneralSettingsPage (PropertiesFile* prefs)
{
    // ── Language selector ────────────────────────────────────────────
    languageLabel_.setText (LvhStr ("STR_LANGUAGE"), dontSendNotification);
    languageLabel_.setFont (Font (12.f));
    languageLabel_.setColour (Label::textColourId, ThemePalette::get (ColourId::TextTertiary));
    addAndMakeVisible (languageLabel_);

    languageCombo_.addItem ("English", 1);
    languageCombo_.addItem (CharPointer_UTF8 ("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"), 2);  // 日本語
    int savedLangId = LanguageManager::getInstance().isJapanese() ? 2 : 1;
    languageCombo_.setSelectedId (savedLangId, dontSendNotification);
    addAndMakeVisible (languageCombo_);

    languageCombo_.onChange = [this, prefs] {
        auto lang = (languageCombo_.getSelectedId() == 2)
                    ? LanguageManager::Language::Japanese
                    : LanguageManager::Language::English;
        LanguageManager::getInstance().setLanguage (lang, prefs);
        if (onLanguageChanged)
            onLanguageChanged (lang == LanguageManager::Language::Japanese ? "ja" : "en");
    };

    // ── Visibility toggles ───────────────────────────────────────────
    auto setup = [&] (ToggleButton& btn, const char* strId, const String& key, bool def) {
        btn.setButtonText (LvhStr (strId));
        btn.setToggleState (prefs ? prefs->getBoolValue (key, def) : def, dontSendNotification);
        addAndMakeVisible (btn);
    };
    setup (showLevelMeter,  "STR_SHOW_LEVEL_METER",   "showLevelMeter",  true);
    setup (showMidiMonitor, "STR_SHOW_MIDI_MONITOR",  "showMidiMonitor", true);
    setup (showInfoMonitor, "STR_SHOW_INFO_MONITOR",  "showInfoMonitor", true);

    showLevelMeter.onClick = [this, prefs] {
        bool v = showLevelMeter.getToggleState();
        if (prefs) prefs->setValue ("showLevelMeter", v);
        if (onShowLevelMeter) onShowLevelMeter (v);
    };
    showMidiMonitor.onClick = [this, prefs] {
        bool v = showMidiMonitor.getToggleState();
        if (prefs) prefs->setValue ("showMidiMonitor", v);
        if (onShowMidiMonitor) onShowMidiMonitor (v);
    };
    showInfoMonitor.onClick = [this, prefs] {
        bool v = showInfoMonitor.getToggleState();
        if (prefs) prefs->setValue ("showInfoMonitor", v);
        if (onShowInfoMonitor) onShowInfoMonitor (v);
    };

    // ── Bridge file browser settings ─────────────────────────────────
    rememberLastFolder.setButtonText (LvhStr ("STR_REMEMBER_FOLDER"));
    rememberLastFolder.setToggleState (
        prefs ? prefs->getBoolValue ("rememberLastFolder", true) : true, dontSendNotification);
    addAndMakeVisible (rememberLastFolder);
    rememberLastFolder.onClick = [this, prefs] {
        if (prefs) prefs->setValue ("rememberLastFolder", rememberLastFolder.getToggleState());
    };

    recentCountLabel.setText (LvhStr ("STR_RECENT_COUNT"), dontSendNotification);
    recentCountLabel.setFont (Font (12.f));
    recentCountLabel.setColour (Label::textColourId, ThemePalette::get (ColourId::TextTertiary));
    addAndMakeVisible (recentCountLabel);

    recentCountSlider.setRange (1, 20, 1);
    recentCountSlider.setValue (prefs ? prefs->getIntValue ("recentBridgeCount", 5) : 5,
                                dontSendNotification);
    recentCountSlider.setTextBoxStyle (Slider::TextBoxRight, false, 36, 22);
    addAndMakeVisible (recentCountSlider);
    recentCountSlider.onValueChange = [this, prefs] {
        if (prefs) prefs->setValue ("recentBridgeCount", (int) recentCountSlider.getValue());
    };

    // ── Color theme ──────────────────────────────────────────────────
    themeLabel_.setText ("Color Theme", dontSendNotification);
    themeLabel_.setFont (Font (12.f));
    themeLabel_.setColour (Label::textColourId, ThemePalette::get (ColourId::TextTertiary));
    addAndMakeVisible (themeLabel_);

    // Scan themes/ directory and populate the combo dynamically.
    // Each .json file may have a "_name" key for its display name.
    {
        auto themesDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                             .getParentDirectory().getChildFile ("themes");
        juce::String savedName = prefs ? prefs->getValue ("themeName", "dark") : "dark";
        int selectedId = 1;

        auto files = themesDir.findChildFiles (juce::File::findFiles, false, "*.json");
        files.sort();  // alphabetical order

        for (auto& f : files)
        {
            juce::String id = f.getFileNameWithoutExtension();
            juce::String displayName = ThemePalette::getDisplayName (f);
            themeIds_.add (id);
            int itemId = themeIds_.size();  // 1-based
            themeCombo_.addItem (displayName, itemId);
            if (id.equalsIgnoreCase (savedName))
                selectedId = itemId;
        }

        // Fallback: if themes/ doesn't exist yet, show built-in options
        if (themeCombo_.getNumItems() == 0)
        {
            themeCombo_.addItem ("Dark",  1);  themeIds_.add ("dark");
            themeCombo_.addItem ("Light", 2);  themeIds_.add ("light");
        }

        themeCombo_.setSelectedId (selectedId, dontSendNotification);
    }
    addAndMakeVisible (themeCombo_);

    themeCombo_.onChange = [this, prefs] {
        int idx = themeCombo_.getSelectedId() - 1;  // 0-based index into themeIds_
        if (idx < 0 || idx >= themeIds_.size()) return;

        juce::String name = themeIds_[idx];
        // Determine LookAndFeel theme (0=Dark, 1=Light) for Mission 051 compatibility
        int lafTheme = name.equalsIgnoreCase ("light") ? 1 : 0;

        if (prefs)
        {
            prefs->setValue ("themeName",   name);
            prefs->setValue ("colorTheme",  lafTheme);
        }
        if (onThemeChanged) onThemeChanged (lafTheme);

        // Notify user that restart is required for the theme to take effect
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            "Theme Changed",
            "The new colour theme will take effect after restarting LVH.",
            "OK");
    };

    // ── Metronome click type ──────────────────────────────────────────
    metroClickLabel_.setText ("Metronome Click", dontSendNotification);
    metroClickLabel_.setFont (Font (12.f));
    metroClickLabel_.setColour (Label::textColourId, ThemePalette::get (ColourId::TextTertiary));
    addAndMakeVisible (metroClickLabel_);

    const int savedClickType = prefs ? prefs->getIntValue ("metronomeClickType", 0) : 0;

    metroNormalBtn_.setButtonText ("Normal");
    metroNormalBtn_.setRadioGroupId (1001);
    metroNormalBtn_.setClickingTogglesState (true);
    metroNormalBtn_.setToggleState (savedClickType == 0, dontSendNotification);
    addAndMakeVisible (metroNormalBtn_);

    metroTechnoBtn_.setButtonText ("Techno");
    metroTechnoBtn_.setRadioGroupId (1001);
    metroTechnoBtn_.setClickingTogglesState (true);
    metroTechnoBtn_.setToggleState (savedClickType == 1, dontSendNotification);
    addAndMakeVisible (metroTechnoBtn_);

    auto metroClickChanged = [this, prefs] {
        int v = metroTechnoBtn_.getToggleState() ? 1 : 0;
        if (prefs) prefs->setValue ("metronomeClickType", v);
        if (onMetronomeClickTypeChanged) onMetronomeClickTypeChanged (v);
    };
    metroNormalBtn_.onClick = metroClickChanged;
    metroTechnoBtn_.onClick = metroClickChanged;
}

void GeneralSettingsPage::refreshLanguage()
{
    languageLabel_.setText (LvhStr ("STR_LANGUAGE"), dontSendNotification);
    showLevelMeter .setButtonText (LvhStr ("STR_SHOW_LEVEL_METER"));
    showMidiMonitor.setButtonText (LvhStr ("STR_SHOW_MIDI_MONITOR"));
    showInfoMonitor.setButtonText (LvhStr ("STR_SHOW_INFO_MONITOR"));
    rememberLastFolder.setButtonText (LvhStr ("STR_REMEMBER_FOLDER"));
    recentCountLabel.setText (LvhStr ("STR_RECENT_COUNT"), dontSendNotification);

    // Sync combo to current language without firing onChange
    int id = LanguageManager::getInstance().isJapanese() ? 2 : 1;
    languageCombo_.setSelectedId (id, dontSendNotification);
}

void GeneralSettingsPage::resized()
{
    auto area = getLocalBounds().reduced (20, 16);

    // Language row
    auto langRow = area.removeFromTop (30);
    languageLabel_.setBounds (langRow.removeFromLeft (90));
    languageCombo_.setBounds (langRow.removeFromLeft (150));
    area.removeFromTop (14);

    for (auto* btn : { &showLevelMeter, &showMidiMonitor, &showInfoMonitor })
    {
        btn->setBounds (area.removeFromTop (30));
        area.removeFromTop (8);
    }
    area.removeFromTop (16);
    rememberLastFolder.setBounds (area.removeFromTop (30));
    area.removeFromTop (12);
    auto row = area.removeFromTop (30);
    recentCountLabel .setBounds (row.removeFromLeft (200));
    recentCountSlider.setBounds (row);
    area.removeFromTop (16);

    // Color theme row
    auto themeRow = area.removeFromTop (26);
    themeLabel_.setBounds (themeRow.removeFromLeft (90));
    themeCombo_.setBounds (themeRow.removeFromLeft (120));
    area.removeFromTop (14);

    // Metronome click type row: label | [Normal] [Techno]
    auto metroRow = area.removeFromTop (26);
    metroClickLabel_.setBounds (metroRow.removeFromLeft (120));
    metroNormalBtn_ .setBounds (metroRow.removeFromLeft (72).reduced (2));
    metroTechnoBtn_ .setBounds (metroRow.removeFromLeft (72).reduced (2));
}

// =====================================================================
// AudioMidiSettingsPage
// =====================================================================
AudioMidiSettingsPage::AudioMidiSettingsPage (AudioDeviceManager& dm)
{
    selector = std::make_unique<AudioDeviceSelectorComponent>
        (dm, 0, 2, 2, 2, true, true, true, false);
    viewport_.setScrollBarsShown (true, false);
    viewport_.setViewedComponent (selector.get(), false);
    addAndMakeVisible (viewport_);
}

void AudioMidiSettingsPage::resized()
{
    viewport_.setBounds (getLocalBounds());
    const int sbW = viewport_.getScrollBarThickness();
    selector->setSize (getWidth() - sbW, 700);
}

// =====================================================================
// PluginPathsPage
// =====================================================================
PluginPathsPage::PluginPathsPage (PropertiesFile* p) : prefs (p)
{
    loadPaths();

    pathList.setColour (ListBox::backgroundColourId, ThemePalette::get (ColourId::ListBg));
    pathList.setColour (ListBox::outlineColourId,    ThemePalette::get (ColourId::SettingsSepLine));
    pathList.setOutlineThickness (1);
    pathList.setRowHeight (24);
    addAndMakeVisible (pathList);

    addBtn.setButtonText (LvhStr ("STR_ADD_PATH"));
    addBtn.setColour (TextButton::buttonColourId, ThemePalette::get (ColourId::CtrlNormal));
    addAndMakeVisible (addBtn);
    addBtn.onClick = [this] {
        auto chooser = std::make_shared<FileChooser> (LvhStr ("STR_SELECT_SCAN_FOLDER"),
                                                      File::getSpecialLocation (File::userHomeDirectory));
        chooser->launchAsync (
            FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
            [this, chooser] (const FileChooser& fc) {
                auto f = fc.getResult();
                if (f.isDirectory() && ! paths.contains (f.getFullPathName()))
                {
                    paths.add (f.getFullPathName());
                    savePaths();
                    pathList.updateContent();
                    if (onPathsChanged) onPathsChanged();
                }
            });
    };

    rmBtn.setButtonText (LvhStr ("STR_REMOVE"));
    rmBtn.setColour (TextButton::buttonColourId, ThemePalette::get (ColourId::CtrlNormal));
    addAndMakeVisible (rmBtn);
    rmBtn.onClick = [this] {
        int sel = pathList.getSelectedRow();
        if (sel >= 0 && sel < paths.size())
        {
            paths.remove (sel);
            savePaths();
            pathList.updateContent();
            if (onPathsChanged) onPathsChanged();
        }
    };

    rescanBtn.setButtonText (LvhStr ("STR_RESCAN_PLUGINS"));
    rescanBtn.setColour (TextButton::buttonColourId, ThemePalette::get (ColourId::SettingsRescan));
    addAndMakeVisible (rescanBtn);
    rescanBtn.onClick = [this] {
        if (onPathsChanged) onPathsChanged();
    };
}

void PluginPathsPage::loadPaths()
{
    paths = StringArray::fromTokens (
        prefs ? prefs->getValue ("pluginScanPaths") : String(), "|", "");
    paths.removeEmptyStrings();
}

void PluginPathsPage::savePaths()
{
    if (prefs) prefs->setValue ("pluginScanPaths", paths.joinIntoString ("|"));
}

void PluginPathsPage::refreshLanguage()
{
    addBtn   .setButtonText (LvhStr ("STR_ADD_PATH"));
    rmBtn    .setButtonText (LvhStr ("STR_REMOVE"));
    rescanBtn.setButtonText (LvhStr ("STR_RESCAN_PLUGINS"));
}

int PluginPathsPage::getNumRows() { return paths.size(); }

void PluginPathsPage::paintListBoxItem (int row, Graphics& g, int width, int height, bool selected)
{
    if (selected)
    {
        g.setColour (ThemePalette::get (ColourId::SettingsSelected));
        g.fillAll();
    }
    g.setColour (selected ? ThemePalette::get (ColourId::SettingsPathText) : ThemePalette::get (ColourId::TextTertiary));
    g.setFont (Font (12.f));
    g.drawText (paths[row], 8, 0, width - 8, height, Justification::centredLeft, true);
}

void PluginPathsPage::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto row  = area.removeFromBottom (32);
    addBtn   .setBounds (row.removeFromLeft (110).reduced (2));
    rmBtn    .setBounds (row.removeFromLeft (80) .reduced (2));
    rescanBtn.setBounds (row.removeFromRight (140).reduced (2));
    pathList .setBounds (area.reduced (0, 4));
}

// =====================================================================
// MidiSettingsPage
// =====================================================================
MidiSettingsPage::MidiSettingsPage (PropertiesFile* prefs)
    : prefs_ (prefs)
{
    // ── Transpose ────────────────────────────────────────────────────────
    tpHeader.setText (LvhStr ("STR_TRANSPOSE"), dontSendNotification);
    tpHeader.setFont (Font (12.f));
    tpHeader.setColour (Label::textColourId, ThemePalette::get (ColourId::TextTertiary));
    addAndMakeVisible (tpHeader);

    tpSlider.setRange (-24, 24, 1);
    tpSlider.setValue (prefs ? prefs->getIntValue ("transpose", 0) : 0, dontSendNotification);
    tpSlider.setTextBoxStyle (Slider::TextBoxRight, false, 44, 22);
    addAndMakeVisible (tpSlider);

    tpSlider.onValueChange = [this, prefs] {
        int v = (int) tpSlider.getValue();
        if (prefs) prefs->setValue ("transpose", v);
        if (onTransposeChange) onTransposeChange (v);
    };

    // ── Channel filter ───────────────────────────────────────────────────
    chHeader.setText (LvhStr ("STR_CHANNEL_FILTER"), dontSendNotification);
    chHeader.setFont (Font (12.f));
    chHeader.setColour (Label::textColourId, ThemePalette::get (ColourId::TextTertiary));
    addAndMakeVisible (chHeader);

    chCombo.addItem (LvhStr ("STR_ALL_CHANNELS"), 1);
    for (int i = 1; i <= 16; ++i)
        chCombo.addItem (LvhStr ("STR_CHANNEL") + String (i), i + 1);
    chCombo.setSelectedId ((prefs ? prefs->getIntValue ("channelFilter", 0) : 0) + 1,
                           dontSendNotification);
    addAndMakeVisible (chCombo);

    chCombo.onChange = [this, prefs] {
        int v = chCombo.getSelectedId() - 1;
        if (prefs) prefs->setValue ("channelFilter", v);
        if (onChannelFilterChange) onChannelFilterChange (v);
    };

    // ── Stage Remote Control ─────────────────────────────────────────────
    auto makeLabel = [this] (Label& lbl, const char* strId)
    {
        lbl.setText (LvhStr (strId), dontSendNotification);
        lbl.setFont (Font (12.f));
        lbl.setColour (Label::textColourId, ThemePalette::get (ColourId::TextTertiary));
        addAndMakeVisible (lbl);
    };

    remoteHeader_.setFont (Font (12.f, Font::bold));
    remoteHeader_.setColour (Label::textColourId, ThemePalette::get (ColourId::TextSettingsHeader));
    remoteHeader_.setText (LvhStr ("STR_STAGE_REMOTE"), dontSendNotification);
    addAndMakeVisible (remoteHeader_);

    makeLabel (methodLabel_,   "STR_REMOTE_METHOD");
    makeLabel (remoteChanLabel_, "STR_REMOTE_CH");
    makeLabel (ccPrevLabel_,   "STR_REMOTE_CC_PREV");
    makeLabel (ccNextLabel_,   "STR_REMOTE_CC_NEXT");
    makeLabel (ccLoadLabel_,   "STR_REMOTE_CC_LOAD");

    // Method combo: None / Program Change / CC
    methodCombo_.addItem (LvhStr ("STR_REMOTE_NONE"), 1);
    methodCombo_.addItem (LvhStr ("STR_REMOTE_PC"),   2);
    methodCombo_.addItem (LvhStr ("STR_REMOTE_CC"),   3);
    methodCombo_.setSelectedId ((prefs ? prefs->getIntValue ("stageRemoteMethod", 0) : 0) + 1,
                                dontSendNotification);
    addAndMakeVisible (methodCombo_);

    // Remote channel combo: Any / 1-16
    remoteChanCombo_.addItem (LvhStr ("STR_ALL_CHANNELS"), 1);
    for (int i = 1; i <= 16; ++i)
        remoteChanCombo_.addItem (LvhStr ("STR_CHANNEL") + String (i), i + 1);
    remoteChanCombo_.setSelectedId ((prefs ? prefs->getIntValue ("stageRemoteChannel", 0) : 0) + 1,
                                    dontSendNotification);
    addAndMakeVisible (remoteChanCombo_);

    // CC number sliders (0-127)
    auto makeCC = [this] (Slider& s, const char* prefKey, int defaultVal)
    {
        s.setRange (0, 127, 1);
        s.setValue (prefs_ ? prefs_->getIntValue (prefKey, defaultVal) : defaultVal,
                    dontSendNotification);
        s.setTextBoxStyle (Slider::TextBoxRight, false, 44, 22);
        s.onValueChange = [this, &s, prefKey] {
            if (prefs_) prefs_->setValue (prefKey, (int) s.getValue());
        };
        addAndMakeVisible (s);
    };
    makeCC (ccPrevSlider_, "stageRemoteCCPrev", 21);
    makeCC (ccNextSlider_, "stageRemoteCCNext", 22);
    makeCC (ccLoadSlider_, "stageRemoteCCLoad", 23);

    methodCombo_.onChange = [this] {
        int v = methodCombo_.getSelectedId() - 1;
        if (prefs_) prefs_->setValue ("stageRemoteMethod", v);
        updateCCVisibility();
        resized();
    };

    remoteChanCombo_.onChange = [this] {
        if (prefs_) prefs_->setValue ("stageRemoteChannel",
                                      remoteChanCombo_.getSelectedId() - 1);
    };

    updateCCVisibility();
}

void MidiSettingsPage::updateCCVisibility()
{
    bool ccMode = (methodCombo_.getSelectedId() == 3);
    ccPrevLabel_ .setVisible (ccMode);
    ccNextLabel_ .setVisible (ccMode);
    ccLoadLabel_ .setVisible (ccMode);
    ccPrevSlider_.setVisible (ccMode);
    ccNextSlider_.setVisible (ccMode);
    ccLoadSlider_.setVisible (ccMode);
}

void MidiSettingsPage::refreshLanguage()
{
    tpHeader.setText (LvhStr ("STR_TRANSPOSE"),      dontSendNotification);
    chHeader.setText (LvhStr ("STR_CHANNEL_FILTER"), dontSendNotification);

    int savedId = chCombo.getSelectedId();
    chCombo.clear (dontSendNotification);
    chCombo.addItem (LvhStr ("STR_ALL_CHANNELS"), 1);
    for (int i = 1; i <= 16; ++i)
        chCombo.addItem (LvhStr ("STR_CHANNEL") + String (i), i + 1);
    chCombo.setSelectedId (savedId, dontSendNotification);

    // Stage Remote labels
    remoteHeader_.setText  (LvhStr ("STR_STAGE_REMOTE"),   dontSendNotification);
    methodLabel_  .setText (LvhStr ("STR_REMOTE_METHOD"),  dontSendNotification);
    remoteChanLabel_.setText(LvhStr ("STR_REMOTE_CH"),     dontSendNotification);
    ccPrevLabel_  .setText (LvhStr ("STR_REMOTE_CC_PREV"), dontSendNotification);
    ccNextLabel_  .setText (LvhStr ("STR_REMOTE_CC_NEXT"), dontSendNotification);
    ccLoadLabel_  .setText (LvhStr ("STR_REMOTE_CC_LOAD"), dontSendNotification);

    int savedMethod = methodCombo_.getSelectedId();
    methodCombo_.clear (dontSendNotification);
    methodCombo_.addItem (LvhStr ("STR_REMOTE_NONE"), 1);
    methodCombo_.addItem (LvhStr ("STR_REMOTE_PC"),   2);
    methodCombo_.addItem (LvhStr ("STR_REMOTE_CC"),   3);
    methodCombo_.setSelectedId (savedMethod, dontSendNotification);

    int savedChan = remoteChanCombo_.getSelectedId();
    remoteChanCombo_.clear (dontSendNotification);
    remoteChanCombo_.addItem (LvhStr ("STR_ALL_CHANNELS"), 1);
    for (int i = 1; i <= 16; ++i)
        remoteChanCombo_.addItem (LvhStr ("STR_CHANNEL") + String (i), i + 1);
    remoteChanCombo_.setSelectedId (savedChan, dontSendNotification);
}

void MidiSettingsPage::resized()
{
    auto area = getLocalBounds().reduced (16, 12);

    // ── Transpose + Channel filter ───────────────────────────────────────
    tpHeader.setBounds (area.removeFromTop (22));
    tpSlider.setBounds (area.removeFromTop (32));
    area.removeFromTop (16);
    chHeader.setBounds (area.removeFromTop (22));
    chCombo .setBounds (area.removeFromTop (28).removeFromLeft (200));
    area.removeFromTop (20);

    // ── Stage Remote Control ─────────────────────────────────────────────
    remoteHeader_   .setBounds (area.removeFromTop (22));
    area.removeFromTop (4);

    auto row = area.removeFromTop (26);
    methodLabel_    .setBounds (row.removeFromLeft (130));
    methodCombo_    .setBounds (row.removeFromLeft (200));
    area.removeFromTop (4);

    row = area.removeFromTop (26);
    remoteChanLabel_.setBounds (row.removeFromLeft (130));
    remoteChanCombo_.setBounds (row.removeFromLeft (200));
    area.removeFromTop (8);

    // CC rows (hidden unless CC mode active)
    for (auto [lbl, sld] : { std::pair<Label*, Slider*>{ &ccPrevLabel_, &ccPrevSlider_ },
                              { &ccNextLabel_, &ccNextSlider_ },
                              { &ccLoadLabel_, &ccLoadSlider_ } })
    {
        row = area.removeFromTop (26);
        lbl->setBounds (row.removeFromLeft (100));
        sld->setBounds (row.removeFromLeft (220));
        area.removeFromTop (4);
    }
}

// =====================================================================
// VisualizerSettingsPage
// =====================================================================
VisualizerSettingsPage::VisualizerSettingsPage (PropertiesFile* prefs)
{
    // ── VU Meter section header ───────────────────────────────────────
    vuHeader_.setText (LvhStr ("STR_VIS_VU_HEADER"), dontSendNotification);
    vuHeader_.setFont (Font (12.f, Font::bold));
    vuHeader_.setColour (Label::textColourId, ThemePalette::get (ColourId::TextSettingsHeader));
    addAndMakeVisible (vuHeader_);

    // ── Theme label ───────────────────────────────────────────────────
    themeLabel_.setText (LvhStr ("STR_VIS_VU_THEME"), dontSendNotification);
    themeLabel_.setFont (Font (12.f));
    themeLabel_.setColour (Label::textColourId, ThemePalette::get (ColourId::TextTertiary));
    addAndMakeVisible (themeLabel_);

    // ── Theme radio buttons ───────────────────────────────────────────
    const int savedTheme = prefs ? prefs->getIntValue ("vuMeterTheme", 0) : 0;

    warmBtn_.setButtonText (LvhStr ("STR_VIS_WARM"));
    warmBtn_.setRadioGroupId (2001);
    warmBtn_.setClickingTogglesState (true);
    warmBtn_.setToggleState (savedTheme == 0, dontSendNotification);
    addAndMakeVisible (warmBtn_);

    neonBtn_.setButtonText (LvhStr ("STR_VIS_NEON"));
    neonBtn_.setRadioGroupId (2001);
    neonBtn_.setClickingTogglesState (true);
    neonBtn_.setToggleState (savedTheme == 1, dontSendNotification);
    addAndMakeVisible (neonBtn_);

    auto themeChanged = [this, prefs] {
        int v = neonBtn_.getToggleState() ? 1 : 0;
        if (prefs) prefs->setValue ("vuMeterTheme", v);
        if (onVuThemeChanged) onVuThemeChanged (v);
    };
    warmBtn_.onClick = themeChanged;
    neonBtn_.onClick = themeChanged;

    // ── Opacity slider ────────────────────────────────────────────────
    opacityLabel_.setText (LvhStr ("STR_VIS_OPACITY"), dontSendNotification);
    opacityLabel_.setFont (Font (12.f));
    opacityLabel_.setColour (Label::textColourId, ThemePalette::get (ColourId::TextTertiary));
    addAndMakeVisible (opacityLabel_);

    opacitySlider_.setRange (20, 100, 1);
    opacitySlider_.setValue (prefs ? prefs->getIntValue ("vuMeterOpacity", 90) : 90,
                             dontSendNotification);
    opacitySlider_.setTextBoxStyle (Slider::TextBoxRight, false, 44, 22);
    opacitySlider_.setTextValueSuffix ("%");
    addAndMakeVisible (opacitySlider_);
    opacitySlider_.onValueChange = [this, prefs] {
        int v = (int) opacitySlider_.getValue();
        if (prefs) prefs->setValue ("vuMeterOpacity", v);
        if (onVuOpacityChanged) onVuOpacityChanged (v);
    };
}

void VisualizerSettingsPage::refreshLanguage()
{
    vuHeader_    .setText (LvhStr ("STR_VIS_VU_HEADER"),  dontSendNotification);
    themeLabel_  .setText (LvhStr ("STR_VIS_VU_THEME"),   dontSendNotification);
    opacityLabel_.setText (LvhStr ("STR_VIS_OPACITY"),    dontSendNotification);
    warmBtn_.setButtonText (LvhStr ("STR_VIS_WARM"));
    neonBtn_.setButtonText (LvhStr ("STR_VIS_NEON"));
}

void VisualizerSettingsPage::resized()
{
    auto area = getLocalBounds().reduced (20, 16);

    vuHeader_.setBounds (area.removeFromTop (22));
    area.removeFromTop (8);

    auto row = area.removeFromTop (26);
    themeLabel_.setBounds (row.removeFromLeft (120));
    warmBtn_   .setBounds (row.removeFromLeft (120).reduced (2));
    neonBtn_   .setBounds (row.removeFromLeft (120).reduced (2));
    area.removeFromTop (8);

    row = area.removeFromTop (26);
    opacityLabel_.setBounds (row.removeFromLeft (120));
    opacitySlider_.setBounds (row.removeFromLeft (260));
}

// =====================================================================
// DisabledPluginsPage
// =====================================================================

namespace {

class RestoreRowComponent : public Component
{
public:
    std::function<void()> onRestore;

    RestoreRowComponent()
    {
        restoreBtn_.setButtonText (LvhStr ("STR_PLUGIN_RESTORE"));
        restoreBtn_.setColour (TextButton::buttonColourId,  ThemePalette::get (ColourId::StateSelected));
        restoreBtn_.setColour (TextButton::textColourOffId, Colours::white);
        restoreBtn_.onClick = [this] { if (onRestore) onRestore(); };
        addAndMakeVisible (restoreBtn_);
    }

    void update (const String& name)
    {
        nameLabel_.setText (name, dontSendNotification);
        repaint();
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (4, 2);
        restoreBtn_.setBounds (b.removeFromRight (80));
        b.removeFromRight (6);
        nameLabel_.setBounds (b);
    }

    void paint (Graphics& g) override
    {
        g.setColour (Colours::white);
        g.setFont (Font (13.f));
        g.drawText (nameLabel_.getText(), getLocalBounds().reduced (8, 0).withTrimmedRight (90),
                    Justification::centredLeft);
    }

private:
    Label      nameLabel_;
    TextButton restoreBtn_;
};

} // namespace

DisabledPluginsPage::DisabledPluginsPage()
{
    headerLabel_.setText (LvhStr ("STR_DISABLED_PLUGINS"), dontSendNotification);
    headerLabel_.setColour (Label::textColourId, ThemePalette::get (ColourId::TextSecondary));
    headerLabel_.setFont (Font (13.f, Font::bold));
    addAndMakeVisible (headerLabel_);

    listBox_.setColour (ListBox::backgroundColourId, ThemePalette::get (ColourId::ListBg));
    listBox_.setColour (ListBox::outlineColourId,    ThemePalette::get (ColourId::ListOutline));
    listBox_.setRowHeight (34);
    listBox_.setOutlineThickness (1);
    addAndMakeVisible (listBox_);
}

void DisabledPluginsPage::resized()
{
    auto b = getLocalBounds().reduced (16);
    headerLabel_.setBounds (b.removeFromTop (24));
    b.removeFromTop (6);
    listBox_.setBounds (b);
}

void DisabledPluginsPage::refresh()
{
    items_.clear();
    if (onGetDisabledPlugins)
        items_ = onGetDisabledPlugins();
    listBox_.updateContent();
    listBox_.repaint();
}

int DisabledPluginsPage::getNumRows()
{
    return items_.isEmpty() ? 1 : items_.size();
}

void DisabledPluginsPage::paintListBoxItem (int row, Graphics& g,
                                             int w, int h, bool /*sel*/)
{
    if (items_.isEmpty() && row == 0)
    {
        g.setColour (ThemePalette::get (ColourId::TextInactive));
        g.setFont (Font (12.f, Font::italic));
        g.drawText (LvhStr ("STR_NO_DISABLED_PLUGINS"), 0, 0, w, h,
                    Justification::centred);
    }
}

Component* DisabledPluginsPage::refreshComponentForRow (int row, bool /*selected*/, Component* existing)
{
    if (items_.isEmpty())
    {
        delete existing;
        return nullptr;
    }

    auto* comp = dynamic_cast<RestoreRowComponent*> (existing);
    if (comp == nullptr)
        comp = new RestoreRowComponent();

    if (row >= 0 && row < items_.size())
    {
        const String id = items_[row];
        String displayName = File (id).getFileNameWithoutExtension();
        if (displayName.isEmpty()) displayName = id;
        comp->update (displayName);
        comp->onRestore = [this, id]
        {
            if (onRestorePlugin) onRestorePlugin (id);
            refresh();
        };
    }
    return comp;
}

// =====================================================================
// SettingsWindow
// =====================================================================
SettingsWindow::SettingsWindow (AudioDeviceManager& dm, PropertiesFile* prefs, Callbacks cbs)
    : DocumentWindow (LvhStr ("STR_SETTINGS_TITLE") + " \xe2\x80\x94 LVH",
                      ThemePalette::get (ColourId::BgPrimary), DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    content = std::make_unique<Content> (dm, prefs, cbs);
    setContentOwned (content.release(), true);
    setResizable (true, false);
    setResizeLimits (500, 350, 1200, 900);
    centreWithSize (700, 450);
}

void SettingsWindow::refresh()
{
    if (auto* c = dynamic_cast<Content*> (getContentComponent()))
        c->refresh();
}

void SettingsWindow::closeButtonPressed()
{
    setVisible (false);
}

// =====================================================================
// SettingsWindow::Content
// =====================================================================
SettingsWindow::Content::Content (AudioDeviceManager& dm, PropertiesFile* prefs, Callbacks& cbs)
{
    genPage_ = new GeneralSettingsPage (prefs);
    genPage_->onShowLevelMeter             = cbs.onShowLevelMeter;
    genPage_->onShowMidiMonitor            = cbs.onShowMidiMonitor;
    genPage_->onShowInfoMonitor            = cbs.onShowInfoMonitor;
    genPage_->onLanguageChanged            = cbs.onLanguageChanged;
    genPage_->onMetronomeClickTypeChanged  = cbs.onMetronomeClickTypeChanged;
    genPage_->onThemeChanged               = cbs.onThemeChanged;
    addPage (LvhStr ("STR_NAV_GENERAL"), genPage_);

    addPage (LvhStr ("STR_NAV_AUDIO_MIDI"), new AudioMidiSettingsPage (dm));

    pathsPage_ = new PluginPathsPage (prefs);
    pathsPage_->onPathsChanged = cbs.onPluginPathsChanged;
    addPage (LvhStr ("STR_NAV_PLUGIN_PATHS"), pathsPage_);

    midiPage_ = new MidiSettingsPage (prefs);
    midiPage_->onTransposeChange     = cbs.onTransposeChange;
    midiPage_->onChannelFilterChange = cbs.onChannelFilterChange;
    addPage (LvhStr ("STR_NAV_MIDI_SETTINGS"), midiPage_);

    visualPage_ = new VisualizerSettingsPage (prefs);
    visualPage_->onVuThemeChanged   = cbs.onVuThemeChanged;
    visualPage_->onVuOpacityChanged = cbs.onVuOpacityChanged;
    addPage (LvhStr ("STR_NAV_VISUALIZER"), visualPage_);

    pluginsPage_ = new DisabledPluginsPage();
    pluginsPage_->onGetDisabledPlugins = cbs.onGetDisabledPlugins;
    pluginsPage_->onRestorePlugin      = cbs.onRestorePlugin;
    addPage (LvhStr ("STR_NAV_PLUGINS"), pluginsPage_);

    selectPage (0);
    setSize (700, 450);
}

void SettingsWindow::Content::refresh()
{
    // Update nav labels
    if (names.size() == 6)
    {
        names.set (0, LvhStr ("STR_NAV_GENERAL"));
        names.set (1, LvhStr ("STR_NAV_AUDIO_MIDI"));
        names.set (2, LvhStr ("STR_NAV_PLUGIN_PATHS"));
        names.set (3, LvhStr ("STR_NAV_MIDI_SETTINGS"));
        names.set (4, LvhStr ("STR_NAV_VISUALIZER"));
        names.set (5, LvhStr ("STR_NAV_PLUGINS"));
    }
    repaint();

    // Refresh each page's text content
    if (genPage_)     genPage_    ->refreshLanguage();
    if (midiPage_)    midiPage_   ->refreshLanguage();
    if (pathsPage_)   pathsPage_  ->refreshLanguage();
    if (visualPage_)  visualPage_ ->refreshLanguage();
    if (pluginsPage_) pluginsPage_->refresh();
}

void SettingsWindow::Content::paint (Graphics& g)
{
    // Nav background
    g.setColour (ThemePalette::get (ColourId::ListBg));
    g.fillRect (0, 0, navW, getHeight());

    // Divider
    g.setColour (ThemePalette::get (ColourId::SettingsSepLine));
    g.drawVerticalLine (navW, 0.f, (float) getHeight());

    // Nav items
    for (int i = 0; i < names.size(); ++i)
    {
        Rectangle<int> row (0, i * rowH, navW, rowH);
        bool sel = (i == currentIdx);

        if (sel)
        {
            g.setColour (ThemePalette::get (ColourId::SettingsSelected));
            g.fillRect (row);
            g.setColour (ThemePalette::get (ColourId::BorderActive));
            g.fillRect (0, row.getY(), 3, rowH);
        }

        g.setColour (sel ? ThemePalette::get (ColourId::TextPrimary) : ThemePalette::get (ColourId::TextSecondary));
        g.setFont (Font (12.5f, sel ? Font::bold : Font::plain));
        g.drawText (names[i], row.withTrimmedLeft (12), Justification::centredLeft);
    }
}

void SettingsWindow::Content::mouseDown (const MouseEvent& e)
{
    if (e.x < navW)
    {
        int idx = e.y / rowH;
        if (idx >= 0 && idx < pages.size())
            selectPage (idx);
    }
}

void SettingsWindow::Content::resized()
{
    auto pageArea = Rectangle<int> (navW + 1, 0,
                                    getWidth() - navW - 1, getHeight());
    for (auto* p : pages)
        p->setBounds (pageArea);
}

void SettingsWindow::Content::addPage (const String& name, Component* page)
{
    names.add (name);
    pages.add (page);
    addChildComponent (*page);
}

void SettingsWindow::Content::selectPage (int idx)
{
    if (currentIdx >= 0 && currentIdx < pages.size())
        pages[currentIdx]->setVisible (false);
    currentIdx = idx;
    if (idx >= 0 && idx < pages.size())
    {
        pages[idx]->setVisible (true);
        if (pluginsPage_ != nullptr && pages[idx] == pluginsPage_)
            pluginsPage_->refresh();
    }
    repaint();
}
