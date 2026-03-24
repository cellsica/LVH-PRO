#pragma once
#include <JuceHeader.h>

// =====================================================================
// StageManager
//
// Manages a Stage Set (.stg) — a JSON list of .lvh project files for
// live performance switching.
//
//   - loadSet / saveSet: JSON file I/O
//   - addItem / removeItem / moveItem: list editing
//   - loadItem: fires onProjectLoadRequested for the chosen project
//   - activeIndex: tracks which project is currently loaded
//
// Callbacks wired by LvhProApplication:
//   onProjectLoadRequested — delegate loadProject to ProjectSerializer
//   onSetChanged           — notify StageWindow to refresh its list
// =====================================================================
class StageManager
{
public:
    // ── Item type ─────────────────────────────────────────────────────
    struct Item
    {
        juce::String alias;
        juce::String path;
    };

    StageManager()  = default;
    ~StageManager() = default;

    // ── Callbacks — wired by LvhProApplication ────────────────────────
    std::function<void(const juce::File&)> onProjectLoadRequested;
    std::function<void()>                  onSetChanged;
    std::function<void(const Item&)>       onLoadError;   // fired when .lvh file is missing

    // ── Set I/O ───────────────────────────────────────────────────────
    void newSet  ();
    void loadSet (const juce::File& file);
    void saveSet (const juce::File& file);   // non-const: resets isDirty_

    // ── List editing ──────────────────────────────────────────────────
    void addItem    (const juce::String& alias, const juce::String& path);
    void removeItem (int index);
    void moveItem   (int oldIndex, int newIndex);
    void renameItem (int index, const juce::String& newAlias);

    // ── Project loading ───────────────────────────────────────────────
    // Loads the project at items_[index] and updates activeIndex.
    void loadItem (int index);

    // ── Accessors ─────────────────────────────────────────────────────
    const juce::Array<Item>& getItems()       const noexcept { return items_; }
    int                      getActiveIndex() const noexcept { return activeIndex_; }
    const juce::String&      getSetName()     const noexcept { return setName_; }
    juce::File               getCurrentFile() const noexcept { return currentFile_; }
    bool                     isDirty()        const noexcept { return isDirty_; }

    void setSetName (const juce::String& name) { setName_ = name; isDirty_ = true; if (onSetChanged) onSetChanged(); }

private:
    juce::String       setName_     = "New Set";
    juce::Array<Item>  items_;
    int                activeIndex_ = -1;
    juce::File         currentFile_;
    bool               isDirty_     = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageManager)
};
