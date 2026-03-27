#include "SettingsWindow.h"

// =====================================================================
// GeneralSettingsPage
// =====================================================================
GeneralSettingsPage::GeneralSettingsPage (PropertiesFile* prefs)
{
    // ── Language selector ────────────────────────────────────────────
    languageLabel_.setText (LvhStr ("STR_LANGUAGE"), dontSendNotification);
    languageLabel_.setFont (Font (12.f));
    languageLabel_.setColour (Label::textColourId, Colour (0xffcccccc));
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
    recentCountLabel.setColour (Label::textColourId, Colour (0xffcccccc));
    addAndMakeVisible (recentCountLabel);

    recentCountSlider.setRange (1, 20, 1);
    recentCountSlider.setValue (prefs ? prefs->getIntValue ("recentBridgeCount", 5) : 5,
                                dontSendNotification);
    recentCountSlider.setTextBoxStyle (Slider::TextBoxRight, false, 36, 22);
    addAndMakeVisible (recentCountSlider);
    recentCountSlider.onValueChange = [this, prefs] {
        if (prefs) prefs->setValue ("recentBridgeCount", (int) recentCountSlider.getValue());
    };
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
}

// =====================================================================
// AudioMidiSettingsPage
// =====================================================================
AudioMidiSettingsPage::AudioMidiSettingsPage (AudioDeviceManager& dm)
{
    selector = std::make_unique<AudioDeviceSelectorComponent>
        (dm, 0, 0, 2, 2, true, true, true, false);
    addAndMakeVisible (*selector);
}

void AudioMidiSettingsPage::resized()
{
    selector->setBounds (getLocalBounds());
}

// =====================================================================
// PluginPathsPage
// =====================================================================
PluginPathsPage::PluginPathsPage (PropertiesFile* p) : prefs (p)
{
    loadPaths();

    pathList.setColour (ListBox::backgroundColourId, Colour (0xff1a1a2a));
    pathList.setColour (ListBox::outlineColourId,    Colour (0xff303048));
    pathList.setOutlineThickness (1);
    pathList.setRowHeight (24);
    addAndMakeVisible (pathList);

    addBtn.setButtonText (LvhStr ("STR_ADD_PATH"));
    addBtn.setColour (TextButton::buttonColourId, Colour (0xff333344));
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
    rmBtn.setColour (TextButton::buttonColourId, Colour (0xff333344));
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
    rescanBtn.setColour (TextButton::buttonColourId, Colour (0xff2a3a5a));
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
        g.setColour (Colour (0xff253555));
        g.fillAll();
    }
    g.setColour (selected ? Colours::white : Colour (0xffcccccc));
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
    tpHeader.setColour (Label::textColourId, Colour (0xffcccccc));
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
    chHeader.setColour (Label::textColourId, Colour (0xffcccccc));
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
        lbl.setColour (Label::textColourId, Colour (0xffcccccc));
        addAndMakeVisible (lbl);
    };

    remoteHeader_.setFont (Font (12.f, Font::bold));
    remoteHeader_.setColour (Label::textColourId, Colour (0xffffaa44));
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
// SettingsWindow
// =====================================================================
SettingsWindow::SettingsWindow (AudioDeviceManager& dm, PropertiesFile* prefs, Callbacks cbs)
    : DocumentWindow (LvhStr ("STR_SETTINGS_TITLE") + " \xe2\x80\x94 LVH",
                      Colour (0xff14141f), DocumentWindow::closeButton)
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
    genPage_->onShowLevelMeter  = cbs.onShowLevelMeter;
    genPage_->onShowMidiMonitor = cbs.onShowMidiMonitor;
    genPage_->onShowInfoMonitor = cbs.onShowInfoMonitor;
    genPage_->onLanguageChanged = cbs.onLanguageChanged;
    addPage (LvhStr ("STR_NAV_GENERAL"), genPage_);

    addPage (LvhStr ("STR_NAV_AUDIO_MIDI"), new AudioMidiSettingsPage (dm));

    pathsPage_ = new PluginPathsPage (prefs);
    pathsPage_->onPathsChanged = cbs.onPluginPathsChanged;
    addPage (LvhStr ("STR_NAV_PLUGIN_PATHS"), pathsPage_);

    midiPage_ = new MidiSettingsPage (prefs);
    midiPage_->onTransposeChange     = cbs.onTransposeChange;
    midiPage_->onChannelFilterChange = cbs.onChannelFilterChange;
    addPage (LvhStr ("STR_NAV_MIDI_SETTINGS"), midiPage_);

    selectPage (0);
    setSize (700, 450);
}

void SettingsWindow::Content::refresh()
{
    // Update nav labels
    if (names.size() == 4)
    {
        names.set (0, LvhStr ("STR_NAV_GENERAL"));
        names.set (1, LvhStr ("STR_NAV_AUDIO_MIDI"));
        names.set (2, LvhStr ("STR_NAV_PLUGIN_PATHS"));
        names.set (3, LvhStr ("STR_NAV_MIDI_SETTINGS"));
    }
    repaint();

    // Refresh each page's text content
    if (genPage_)   genPage_  ->refreshLanguage();
    if (midiPage_)  midiPage_ ->refreshLanguage();
    if (pathsPage_) pathsPage_->refreshLanguage();
}

void SettingsWindow::Content::paint (Graphics& g)
{
    // Nav background
    g.setColour (Colour (0xff1a1a2a));
    g.fillRect (0, 0, navW, getHeight());

    // Divider
    g.setColour (Colour (0xff303048));
    g.drawVerticalLine (navW, 0.f, (float) getHeight());

    // Nav items
    for (int i = 0; i < names.size(); ++i)
    {
        Rectangle<int> row (0, i * rowH, navW, rowH);
        bool sel = (i == currentIdx);

        if (sel)
        {
            g.setColour (Colour (0xff253555));
            g.fillRect (row);
            g.setColour (Colour (0xff4488dd));
            g.fillRect (0, row.getY(), 3, rowH);
        }

        g.setColour (sel ? Colours::white : Colour (0xff999aaa));
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
        pages[idx]->setVisible (true);
    repaint();
}
