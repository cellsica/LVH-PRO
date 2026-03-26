#include "StageManager.h"

// ── Set I/O ───────────────────────────────────────────────────────────────

void StageManager::newSet()
{
    setName_     = "New Set";
    items_.clear();
    activeIndex_ = -1;
    currentFile_ = juce::File{};
    isDirty_     = false;
    if (onSetChanged) onSetChanged();
}

void StageManager::loadSet (const juce::File& file)
{
    if (! file.existsAsFile()) return;

    auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject()) return;

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) return;

    setName_     = obj->getProperty ("setName").toString();
    if (setName_.isEmpty()) setName_ = file.getFileNameWithoutExtension();

    items_.clear();
    activeIndex_ = -1;
    currentFile_ = file;
    isDirty_     = false;

    if (auto* arr = obj->getProperty ("items").getArray())
    {
        for (auto& v : *arr)
        {
            if (auto* item = v.getDynamicObject())
            {
                Item it;
                it.alias = item->getProperty ("alias").toString();
                it.path  = item->getProperty ("path").toString();
                if (it.path.isNotEmpty())
                    items_.add (it);
            }
        }
    }

    if (onSetChanged) onSetChanged();
}

void StageManager::saveSet (const juce::File& file)
{
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty ("setName", setName_);

    juce::Array<juce::var> arr;
    for (auto& it : items_)
    {
        auto itemObj = std::make_unique<juce::DynamicObject>();
        itemObj->setProperty ("alias", it.alias);
        itemObj->setProperty ("path",  it.path);
        arr.add (itemObj.release());
    }
    obj->setProperty ("items", arr);

    juce::var root (obj.release());
    file.replaceWithText (juce::JSON::toString (root, true));
    isDirty_ = false;
}

// ── List editing ──────────────────────────────────────────────────────────

void StageManager::addItem (const juce::String& alias, const juce::String& path)
{
    Item it;
    it.alias = alias.isNotEmpty() ? alias : juce::File (path).getFileNameWithoutExtension();
    it.path  = path;
    items_.add (it);
    isDirty_ = true;

    if (onSetChanged) onSetChanged();
}

void StageManager::removeItem (int index)
{
    if (index < 0 || index >= items_.size()) return;

    items_.remove (index);

    if (activeIndex_ == index)
        activeIndex_ = -1;
    else if (activeIndex_ > index)
        --activeIndex_;

    isDirty_ = true;
    if (onSetChanged) onSetChanged();
}

void StageManager::moveItem (int oldIndex, int newIndex)
{
    if (oldIndex == newIndex) return;
    if (oldIndex < 0 || oldIndex >= items_.size()) return;
    if (newIndex < 0 || newIndex >= items_.size()) return;

    auto item = items_[oldIndex];
    items_.remove (oldIndex);
    items_.insert (newIndex, item);

    // Track active index through the move
    if (activeIndex_ == oldIndex)
    {
        activeIndex_ = newIndex;
    }
    else if (oldIndex < newIndex)
    {
        if (activeIndex_ > oldIndex && activeIndex_ <= newIndex) --activeIndex_;
    }
    else
    {
        if (activeIndex_ >= newIndex && activeIndex_ < oldIndex) ++activeIndex_;
    }

    isDirty_ = true;
    if (onSetChanged) onSetChanged();
}

void StageManager::renameItem (int index, const juce::String& newAlias)
{
    if (index < 0 || index >= items_.size()) return;
    items_.getReference (index).alias = newAlias;
    isDirty_ = true;
    if (onSetChanged) onSetChanged();
}

// ── Project loading ───────────────────────────────────────────────────────

void StageManager::loadItem (int index)
{
    if (index < 0 || index >= items_.size()) return;

    juce::File f (items_[index].path);
    if (! f.existsAsFile())
    {
        if (onLoadError) onLoadError (items_[index]);
        return;
    }

    activeIndex_ = index;
    if (onSetChanged) onSetChanged();

    // Slot 0: full load with Global marking.
    // Slot 1+: instrument-only switch (Global Layer mode).
    bool isGlobal          = (index == 0);
    bool globalLayerSwitch = (index > 0);
    if (onProjectLoadRequested) onProjectLoadRequested (f, isGlobal, globalLayerSwitch);
}
