#pragma once
#include <JuceHeader.h>

/**
 * @class StageManager
 * @brief Manages a Stage Set — an ordered list of `.lvh` project files for live performance.
 *
 * A Stage Set (`.stg`) is a JSON file containing an ordered list of project references.
 * Each entry has a user-defined alias and the absolute path to a `.lvh` file.
 *
 * **Slot semantics:**
 * | Slot index | isGlobal | globalLayerSwitch | Behaviour |
 * |------------|----------|-------------------|-----------|
 * | 0          | true     | false             | Full load; all bridges marked Global (persistent layer) |
 * | 1+         | false    | true              | Instrument-only switch; Global layer is preserved |
 *
 * The Global layer (Slot 0) loads master FX, audio settings, and window layout once per set.
 * Subsequent slots swap only the instrument bridges, keeping the Global layer alive.
 *
 * **Thread safety:** All public methods must be called from the message thread.
 */
class StageManager
{
public:
    /**
     * @brief Represents one entry in the Stage Set list.
     */
    struct Item
    {
        juce::String alias;  ///< User-defined display name shown in the StageWindow.
        juce::String path;   ///< Absolute path to the `.lvh` project file.
    };

    StageManager()  = default;
    ~StageManager() = default;

    // ── Callbacks — wired by LvhProApplication ────────────────────────────────

    /**
     * @brief Fired when a slot is requested to load.
     *
     * Delegate to ProjectSerializer::loadProject().
     * @param file               The `.lvh` file to load.
     * @param isGlobal           true for Slot 0 (Global layer).
     * @param globalLayerSwitch  true for Slot 1+ (instrument-only switch).
     */
    std::function<void(const juce::File&, bool isGlobal, bool globalLayerSwitch)> onProjectLoadRequested;

    /** @brief Fired after the set list is modified so StageWindow can refresh its UI. */
    std::function<void()> onSetChanged;

    /**
     * @brief Fired when a slot's `.lvh` file cannot be found on disk.
     * @param item  The item whose file is missing.
     */
    std::function<void(const Item&)> onLoadError;

    // ── Set I/O ───────────────────────────────────────────────────────────────

    /** @brief Reset to an empty, untitled Stage Set. */
    void newSet();

    /**
     * @brief Load a Stage Set from a `.stg` JSON file.
     * @param file  The `.stg` file to open.
     */
    void loadSet (const juce::File& file);

    /**
     * @brief Save the current Stage Set to a `.stg` JSON file.
     *
     * Resets the dirty flag after a successful save.
     *
     * @param file  Destination file path.
     */
    void saveSet (const juce::File& file);

    // ── List editing ──────────────────────────────────────────────────────────

    /**
     * @brief Append a new entry to the Stage Set list.
     * @param alias  Display name for the slot.
     * @param path   Absolute path to the `.lvh` project file.
     */
    void addItem (const juce::String& alias, const juce::String& path);

    /**
     * @brief Remove the entry at @p index from the list.
     * @param index  Zero-based slot index.
     */
    void removeItem (int index);

    /**
     * @brief Move a slot from @p oldIndex to @p newIndex.
     * @param oldIndex  Current position of the slot.
     * @param newIndex  Target position.
     */
    void moveItem (int oldIndex, int newIndex);

    /**
     * @brief Rename the alias of the slot at @p index.
     * @param index     Zero-based slot index.
     * @param newAlias  New display name.
     */
    void renameItem (int index, const juce::String& newAlias);

    // ── Project loading ───────────────────────────────────────────────────────

    /**
     * @brief Load the project at the given slot index.
     *
     * Updates activeIndex_ and fires onProjectLoadRequested with the correct
     * isGlobal / globalLayerSwitch flags based on the slot position.
     * If the file is missing, fires onLoadError instead.
     *
     * @param index  Zero-based slot index to load.
     */
    void loadItem (int index);

    // ── Accessors ─────────────────────────────────────────────────────────────

    /** @brief Returns the full list of Stage Set items. */
    const juce::Array<Item>& getItems() const noexcept { return items_; }

    /**
     * @brief Returns the index of the currently loaded slot.
     * @return Zero-based index, or -1 if no slot has been loaded yet.
     */
    int getActiveIndex() const noexcept { return activeIndex_; }

    /** @brief Returns the user-defined name of this Stage Set. */
    const juce::String& getSetName() const noexcept { return setName_; }

    /**
     * @brief Returns the file path of the currently open `.stg` file.
     * @return The file, or an invalid File if the set has not been saved yet.
     */
    juce::File getCurrentFile() const noexcept { return currentFile_; }

    /**
     * @brief Returns true if the set has unsaved changes.
     * @return true if modified since the last save or load.
     */
    bool isDirty() const noexcept { return isDirty_; }

    /**
     * @brief Set the Stage Set name and mark the set as dirty.
     * @param name  New name for this Stage Set.
     */
    void setSetName (const juce::String& name)
    {
        setName_ = name;
        isDirty_ = true;
        if (onSetChanged) onSetChanged();
    }

private:
    juce::String      setName_     = "New Set";
    juce::Array<Item> items_;
    int               activeIndex_ = -1;
    juce::File        currentFile_;
    bool              isDirty_     = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageManager)
};
