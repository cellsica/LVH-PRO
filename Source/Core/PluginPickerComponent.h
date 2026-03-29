#pragma once
#include <JuceHeader.h>
#include "../UiCommon.h"

// =====================================================================
// PluginPickerComponent
//
// A rich plugin selection panel replacing the simple PopupMenu.
// Provides:
//   - Text search bar with live filtering
//   - All / Favorites toggle
//   - ListBox with plugin name + ★ favorite button per row
//
// Usage:
//   auto* picker = new PluginPickerComponent (plugins, isFavFn, toggleFavFn);
//   picker->onPluginSelected = [&] (const juce::PluginDescription& d) { ... };
//   juce::CallOutBox::launchAsynchronously (
//       std::unique_ptr<Component>(picker), anchorBounds, parentComp);
// =====================================================================
class PluginPickerComponent  : public juce::Component,
                                private juce::TextEditor::Listener,
                                private juce::ListBoxModel
{
public:
    // ── Callbacks ─────────────────────────────────────────────────────────
    std::function<void(const juce::PluginDescription&)> onPluginSelected;
    std::function<void()>                               onRefreshRequested;

    // ── Construction ──────────────────────────────────────────────────────
    PluginPickerComponent (
        const juce::Array<juce::PluginDescription>& allPlugins,
        std::function<bool(const juce::String&)>    isFavFn,
        std::function<void(const juce::String&)>    toggleFavFn);

    ~PluginPickerComponent() override = default;

    // ── Component ─────────────────────────────────────────────────────────
    void resized() override;
    void paint  (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // ── ListBoxModel ──────────────────────────────────────────────────────
    int  getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    juce::Component* refreshComponentForRow (int row, bool selected,
                                             juce::Component* existing) override;

    // ── TextEditor::Listener ──────────────────────────────────────────────
    void textEditorTextChanged (juce::TextEditor&) override;

    // ── Internal helpers ─────────────────────────────────────────────────
    void rebuildFilteredList();
    void selectPlugin (int filteredIndex);

    // ── Data ──────────────────────────────────────────────────────────────
    juce::Array<juce::PluginDescription>    allPlugins_;
    juce::Array<juce::PluginDescription>    filtered_;
    std::function<bool(const juce::String&)>  isFav_;
    std::function<void(const juce::String&)>  toggleFav_;

    // ── UI widgets ────────────────────────────────────────────────────────
    juce::TextEditor  searchBox_;
    juce::TextButton  allBtn_     { "" };
    juce::TextButton  favBtn_     { "" };
    juce::ListBox     listBox_;
    juce::TextButton  refreshBtn_ { "" };

    bool showFavOnly_ = false;

    // ── Row component (inner class) ───────────────────────────────────────
    class PluginRowComponent : public juce::Component
    {
    public:
        std::function<void(int)> onFavToggle;
        std::function<void(int)> onPluginClicked;

        PluginRowComponent();
        void update (const juce::String& name, bool isFav, int rowIndex, bool selected);
        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::Label      nameLabel_;
        juce::DrawableButton favButton_ { "fav", juce::DrawableButton::ImageFitted };
        bool   isFav_    = false;
        int    rowIndex_ = -1;
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPickerComponent)
};
