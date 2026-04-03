#include "PluginPickerComponent.h"
#include "../LanguageManager.h"
#include "ThemePalette.h"

// ─────────────────────────────────────────────────────────────────────────────
// PluginRowComponent
// ─────────────────────────────────────────────────────────────────────────────

PluginPickerComponent::PluginRowComponent::PluginRowComponent()
{
    nameLabel_.setJustificationType (juce::Justification::centredLeft);
    nameLabel_.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel_);

    favButton_.setColour (juce::DrawableButton::backgroundColourId,          juce::Colours::transparentBlack);
    favButton_.setColour (juce::DrawableButton::backgroundOnColourId,        juce::Colours::transparentBlack);
    addAndMakeVisible (favButton_);

    favButton_.onClick = [this] {
        if (onFavToggle) onFavToggle (rowIndex_);
    };
}

void PluginPickerComponent::PluginRowComponent::update (
    const juce::String& name, bool isFav, int rowIndex, bool selected)
{
    isFav_    = isFav;
    rowIndex_ = rowIndex;

    nameLabel_.setText (name, juce::dontSendNotification);

    juce::Colour textCol = selected ? ThemePalette::get (ColourId::BgPanelAlt)
                                    : ThemePalette::get (ColourId::TextPrimary);
    nameLabel_.setColour (juce::Label::textColourId, textCol);

    repaint();
}

void PluginPickerComponent::PluginRowComponent::resized()
{
    auto b = getLocalBounds();
    int  btnSize = b.getHeight();
    favButton_.setBounds (b.removeFromRight (btnSize).reduced (4));
    nameLabel_.setBounds (b.reduced (6, 0));
}

void PluginPickerComponent::PluginRowComponent::paint (juce::Graphics& g)
{
    auto btnBounds = favButton_.getBoundsInParent().toFloat();
    Icons::star (g, btnBounds.reduced (4.f), isFav_);
}

void PluginPickerComponent::PluginRowComponent::mouseUp (const juce::MouseEvent& e)
{
    if (favButton_.getBounds().contains (e.getPosition()))
        return;

    if (e.mods.isRightButtonDown())
    {
        if (onRightClicked) onRightClicked (rowIndex_);
        return;
    }

    if (onPluginClicked) onPluginClicked (rowIndex_);
}

// ─────────────────────────────────────────────────────────────────────────────
// PluginPickerComponent
// ─────────────────────────────────────────────────────────────────────────────

PluginPickerComponent::PluginPickerComponent (
    const juce::Array<juce::PluginDescription>& allPlugins,
    std::function<bool(const juce::String&)>    isFavFn,
    std::function<void(const juce::String&)>    toggleFavFn,
    std::function<bool(const juce::String&)>    isDisabledFn,
    std::function<void(const juce::String&)>    toggleDisabledFn)
    : allPlugins_     (allPlugins),
      isFav_          (std::move (isFavFn)),
      toggleFav_      (std::move (toggleFavFn)),
      isDisabled_     (std::move (isDisabledFn)),
      toggleDisabled_ (std::move (toggleDisabledFn))
{
    // Search box
    searchBox_.setTextToShowWhenEmpty (LvhStr ("STR_SEARCH_PLUGIN"), juce::Colours::grey);
    searchBox_.setColour (juce::TextEditor::backgroundColourId, ThemePalette::get (ColourId::ListBg));
    searchBox_.setColour (juce::TextEditor::outlineColourId,    ThemePalette::get (ColourId::ListOutline));
    searchBox_.setColour (juce::TextEditor::textColourId,       juce::Colours::white);
    searchBox_.addListener (this);
    addAndMakeVisible (searchBox_);

    setWantsKeyboardFocus (true);

    // All / Favorites toggle buttons
    auto styleToggle = [&] (juce::TextButton& btn, const char* strId, bool isActive)
    {
        btn.setButtonText (LvhStr (strId));
        btn.setColour (juce::TextButton::buttonColourId,
                       isActive ? ThemePalette::get (ColourId::CtrlActive) : ThemePalette::get (ColourId::CtrlNormal));
        btn.setColour (juce::TextButton::textColourOffId, ThemePalette::get (ColourId::TextPrimary));
        btn.setColour (juce::TextButton::buttonOnColourId, ThemePalette::get (ColourId::CtrlActive));
        addAndMakeVisible (btn);
    };

    styleToggle (allBtn_, "STR_FILTER_ALL",      true);
    styleToggle (favBtn_, "STR_FILTER_FAVORITE", false);

    allBtn_.onClick = [this] {
        showFavOnly_ = false;
        allBtn_.setColour (juce::TextButton::buttonColourId, ThemePalette::get (ColourId::CtrlActive));
        favBtn_.setColour (juce::TextButton::buttonColourId, ThemePalette::get (ColourId::CtrlNormal));
        rebuildFilteredList();
    };
    favBtn_.onClick = [this] {
        showFavOnly_ = true;
        allBtn_.setColour (juce::TextButton::buttonColourId, ThemePalette::get (ColourId::CtrlNormal));
        favBtn_.setColour (juce::TextButton::buttonColourId, ThemePalette::get (ColourId::CtrlActive));
        rebuildFilteredList();
    };

    // ListBox
    listBox_.setModel (this);
    listBox_.setColour (juce::ListBox::backgroundColourId, ThemePalette::get (ColourId::ListBg));
    listBox_.setColour (juce::ListBox::outlineColourId,    ThemePalette::get (ColourId::ListOutline));
    listBox_.setRowHeight (32);
    listBox_.setOutlineThickness (1);
    addAndMakeVisible (listBox_);

    // Refresh button
    refreshBtn_.setButtonText (LvhStr ("STR_REFRESH_PLUGINS"));
    refreshBtn_.setColour (juce::TextButton::buttonColourId,  ThemePalette::get (ColourId::CtrlNormal));
    refreshBtn_.setColour (juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    refreshBtn_.onClick = [this] {
        if (onRefreshRequested) onRefreshRequested();
    };
    addAndMakeVisible (refreshBtn_);

    setSize (420, 480);
    rebuildFilteredList();
}

void PluginPickerComponent::resized()
{
    auto b = getLocalBounds().reduced (8);

    // Search bar
    searchBox_.setBounds (b.removeFromTop (28));
    b.removeFromTop (6);

    // Toggle row
    auto toggleRow = b.removeFromTop (26);
    int  btnW = toggleRow.getWidth() / 2;
    allBtn_.setBounds (toggleRow.removeFromLeft (btnW).reduced (2, 0));
    favBtn_.setBounds (toggleRow.reduced (2, 0));
    b.removeFromTop (6);

    // Refresh button (bottom)
    refreshBtn_.setBounds (b.removeFromBottom (26));
    b.removeFromBottom (4);

    // List area
    listBox_.setBounds (b);
}

void PluginPickerComponent::paint (juce::Graphics& g)
{
    g.fillAll (ThemePalette::get (ColourId::BgPanelAlt));
}

// ── ListBoxModel ─────────────────────────────────────────────────────────────

int PluginPickerComponent::getNumRows()
{
    return filtered_.size();
}

void PluginPickerComponent::paintListBoxItem (int /*row*/, juce::Graphics& /*g*/,
                                               int /*w*/, int /*h*/, bool /*selected*/)
{
    // Painting is handled by PluginRowComponent (refreshComponentForRow)
}

juce::Component* PluginPickerComponent::refreshComponentForRow (
    int row, bool selected, juce::Component* existing)
{
    auto* comp = dynamic_cast<PluginRowComponent*> (existing);
    if (comp == nullptr)
    {
        comp = new PluginRowComponent();
        comp->onFavToggle = [this] (int rowIdx)
        {
            if (rowIdx >= 0 && rowIdx < filtered_.size())
            {
                toggleFav_ (filtered_[rowIdx].fileOrIdentifier);
                rebuildFilteredList();
            }
        };
        comp->onPluginClicked = [this] (int rowIdx)
        {
            selectPlugin (rowIdx);
        };
        comp->onRightClicked = [this] (int rowIdx)
        {
            if (rowIdx < 0 || rowIdx >= filtered_.size()) return;
            const auto& desc = filtered_[rowIdx];
            juce::PopupMenu menu;
            menu.addItem (1, LvhStr ("STR_PLUGIN_DISABLE"));
            menu.showMenuAsync (juce::PopupMenu::Options{}, [this, id = desc.fileOrIdentifier] (int result)
            {
                if (result == 1)
                {
                    toggleDisabled_ (id);
                    rebuildFilteredList();
                }
            });
        };
    }

    if (row >= 0 && row < filtered_.size())
    {
        const auto& desc = filtered_[row];
        bool fav = isFav_ (desc.fileOrIdentifier);
        comp->update (desc.name, fav, row, selected);
    }
    return comp;
}

// ── TextEditor::Listener ──────────────────────────────────────────────────────

void PluginPickerComponent::textEditorTextChanged (juce::TextEditor&)
{
    rebuildFilteredList();
}

// ── Internal helpers ──────────────────────────────────────────────────────────

void PluginPickerComponent::rebuildFilteredList()
{
    juce::String query = searchBox_.getText().trim().toLowerCase();

    filtered_.clear();
    for (const auto& desc : allPlugins_)
    {
        if (isDisabled_ && isDisabled_ (desc.fileOrIdentifier))
            continue;
        if (showFavOnly_ && ! isFav_ (desc.fileOrIdentifier))
            continue;
        if (query.isNotEmpty() && ! desc.name.toLowerCase().contains (query))
            continue;
        filtered_.add (desc);
    }

    listBox_.updateContent();
    listBox_.repaint();
}

void PluginPickerComponent::selectPlugin (int filteredIndex)
{
    if (filteredIndex >= 0 && filteredIndex < filtered_.size())
    {
        if (onPluginSelected)
            onPluginSelected (filtered_[filteredIndex]);
        // Keep the picker open so the user can launch multiple plugins in a row.
        // Use the X button or ESC to close.
    }
}

bool PluginPickerComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        // Works whether hosted in a DocumentWindow or a CallOutBox
        if (auto* w = findParentComponentOfClass<juce::ResizableWindow>())
            w->setVisible (false);
        else if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
            cb->dismiss();
        return true;
    }
    return false;
}
