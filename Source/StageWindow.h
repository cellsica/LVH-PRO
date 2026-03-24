#pragma once
#include "UiCommon.h"
#include "Core/StageManager.h"

// Returns a font that supports Japanese text on Windows 10/11 ("Yu Gothic UI").
// Falls back gracefully to the JUCE default on other platforms.
static inline juce::Font stageFont (float size, bool bold = false)
{
    int style = bold ? juce::Font::bold : juce::Font::plain;
    return juce::Font ("Yu Gothic UI", size, style);
}

// =====================================================================
// SongStrip — one row in the stage setlist
//
// Visual states:
//   Active   (green bg + left bar): project is currently loaded
//   Selected (blue border):         row is selected but not yet loaded
//   Default:                        neither
//
// Interactions:
//   Left-click body  → onSelected  (selection only, no load)
//   LOAD button      → onLoadClicked (activates project)
//   Right-click      → context menu: Rename Alias / Delete
//   Drag body        → onDragUpdate / onDragEnd for list reordering
// =====================================================================
class SongStrip : public juce::Component
{
public:
    static constexpr int kHeight        = 64;
    static constexpr int kDragThreshold =  6;

    std::function<void()>      onLoadClicked;
    std::function<void()>      onSelected;
    std::function<void(float)> onDragUpdate;   // y-pos in parent coords during drag
    std::function<void()>      onDragEnd;
    std::function<void()>      onRenameAlias;
    std::function<void()>      onDelete;

    SongStrip()
    {
        loadBtn_.setButtonText (LvhStr ("STR_LOAD"));
        loadBtn_.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1a3a1a));
        loadBtn_.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff22cc44));
        loadBtn_.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        loadBtn_.onClick = [this] { if (onLoadClicked) onLoadClicked(); };
        addAndMakeVisible (loadBtn_);
    }

    void setItem (const juce::String& alias, const juce::String& path,
                  int index, bool active, bool selected)
    {
        alias_    = alias;
        path_     = path;
        index_    = index;
        active_   = active;
        selected_ = selected;
        repaint();
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6, 8);
        loadBtn_.setBounds (b.removeFromRight (70));
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds();

        // Background
        if (active_)
            g.fillAll (juce::Colour (0xff0d3d1a));
        else if (selected_)
            g.fillAll (juce::Colour (0xff1e1e30));
        else
            g.fillAll (juce::Colour (0xff1a1a24));

        // Active: green left bar
        if (active_)
        {
            g.setColour (juce::Colour (0xff22cc44));
            g.fillRect (0, 0, 4, b.getHeight());
        }

        // Selected (not active): blue border
        if (selected_ && ! active_)
        {
            g.setColour (juce::Colour (0xff4488cc));
            g.drawRect (getLocalBounds().toFloat().reduced (0.75f), 1.5f);
        }

        // Index badge
        auto badge = b.removeFromLeft (36);
        g.setColour (active_    ? juce::Colour (0xff22cc44)
                    : selected_ ? juce::Colour (0xff4488cc)
                    :             juce::Colour (0xff444455));
        g.setFont (stageFont (16.f, true));
        g.drawText (juce::String (index_ + 1), badge, juce::Justification::centred);

        // Alias + path text
        auto textArea = b.reduced (4, 0).withTrimmedRight (82);
        g.setColour (active_    ? juce::Colour (0xff88ffaa)
                    : selected_ ? juce::Colour (0xffaaccff)
                    :             juce::Colours::white);
        g.setFont (stageFont (16.f, true));
        g.drawText (alias_, textArea.removeFromTop (textArea.getHeight() / 2),
                    juce::Justification::centredLeft, true);
        g.setColour (juce::Colour (0xff666677));
        g.setFont (stageFont (11.f));
        g.drawText (path_, textArea, juce::Justification::centredLeft, true);

        // Row separator
        g.setColour (juce::Colour (0xff2a2a38));
        g.drawHorizontalLine (getHeight() - 1, 0.f, (float) getWidth());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            showContextMenu();
            return;
        }

        if (! loadBtn_.getBounds().contains (e.getPosition()))
        {
            dragStartY_ = e.y;
            isDragging_ = false;
            if (onSelected) onSelected();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) return;
        if (loadBtn_.getBounds().contains (e.getMouseDownPosition())) return;

        if (! isDragging_ && std::abs (e.y - dragStartY_) > kDragThreshold)
            isDragging_ = true;

        if (isDragging_)
            if (onDragUpdate) onDragUpdate ((float) (getY() + e.y));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (isDragging_)
        {
            isDragging_ = false;
            if (onDragEnd) onDragEnd();
        }
    }

private:
    void showContextMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, LvhStr ("STR_RENAME_ALIAS"));
        m.addItem (2, LvhStr ("STR_DELETE"));
        m.showMenuAsync (juce::PopupMenu::Options(),
            [this] (int result)
            {
                if (result == 1 && onRenameAlias) onRenameAlias();
                if (result == 2 && onDelete)       onDelete();
            });
    }

    juce::TextButton loadBtn_;
    juce::String     alias_, path_;
    int              index_      = 0;
    bool             active_     = false;
    bool             selected_   = false;
    int              dragStartY_ = 0;
    bool             isDragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongStrip)
};

// =====================================================================
// StageListComponent — scrollable list of SongStrips with D&D reorder
//
// Drag reorder:
//   SongStrip reports mouse-Y via onDragUpdate → we compute insert slot
//   and paint a green drop-line via paintOverChildren.
//   On onDragEnd we call onItemMove(from, to).
// =====================================================================
class StageListComponent : public juce::Component
{
public:
    StageListComponent() = default;

    std::function<void(int)>      onItemLoad;
    std::function<void(int)>      onItemSelected;
    std::function<void(int, int)> onItemMove;
    std::function<void(int)>      onItemRename;
    std::function<void(int)>      onItemDelete;

    void refresh (const juce::Array<StageManager::Item>& items,
                  int activeIndex, int selectedIndex)
    {
        strips_.clear();

        for (int i = 0; i < items.size(); ++i)
        {
            auto* strip = strips_.add (std::make_unique<SongStrip>());
            strip->setItem (items[i].alias, items[i].path,
                            i, i == activeIndex, i == selectedIndex);
            int idx = i;
            strip->onLoadClicked = [this, idx] { if (onItemLoad)     onItemLoad     (idx); };
            strip->onSelected    = [this, idx] { if (onItemSelected) onItemSelected (idx); };
            strip->onDragUpdate  = [this, idx] (float y) { handleDragUpdate (idx, y); };
            strip->onDragEnd     = [this, idx] { handleDragEnd (idx); };
            strip->onRenameAlias = [this, idx] { if (onItemRename)   onItemRename   (idx); };
            strip->onDelete      = [this, idx] { if (onItemDelete)   onItemDelete   (idx); };
            addAndMakeVisible (strip);
        }

        int totalH = items.size() * SongStrip::kHeight;
        setSize (getWidth(), juce::jmax (1, totalH));
        resized();
    }

    void resized() override
    {
        int y = 0;
        for (auto& s : strips_)
        {
            s->setBounds (0, y, getWidth(), SongStrip::kHeight);
            y += SongStrip::kHeight;
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (dragFromIndex_ < 0 || dropAtIndex_ < 0) return;

        int lineY = dropAtIndex_ * SongStrip::kHeight;
        g.setColour (juce::Colour (0xff22cc44));
        g.fillRect (8, lineY - 2, getWidth() - 16, 4);
    }

private:
    void handleDragUpdate (int fromIndex, float yInComponent)
    {
        dragFromIndex_ = fromIndex;
        int slots   = strips_.size();
        int newDrop = juce::roundToInt (yInComponent / (float) SongStrip::kHeight);
        newDrop     = juce::jlimit (0, slots, newDrop);

        if (newDrop != dropAtIndex_)
        {
            dropAtIndex_ = newDrop;
            repaint();
        }
    }

    void handleDragEnd (int fromIndex)
    {
        int target     = dropAtIndex_;
        dragFromIndex_ = -1;
        dropAtIndex_   = -1;
        repaint();

        // Convert insert-position to move-target index
        if (target >= 0 && target != fromIndex && target != fromIndex + 1)
        {
            int moveTo = (target > fromIndex) ? target - 1 : target;
            if (moveTo != fromIndex && onItemMove)
                onItemMove (fromIndex, moveTo);
        }
    }

    juce::OwnedArray<SongStrip> strips_;
    int dragFromIndex_ = -1;
    int dropAtIndex_   = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageListComponent)
};

// =====================================================================
// StageContentComponent — toolbar + editable set-name label + list
// =====================================================================
class StageContentComponent : public juce::Component,
                               private juce::Label::Listener
{
public:
    std::function<void(int)> onItemLoad;
    std::function<void()>    onAddClicked;
    std::function<void()>    onSaveSetClicked;
    std::function<void()>    onLoadSetClicked;
    std::function<void()>    onNewSetClicked;

    std::function<void(bool)> onPinToggled;

    explicit StageContentComponent (StageManager& manager)
        : manager_ (manager)
    {
        // ── Toolbar buttons ──────────────────────────────────────────
        configureButton (newBtn_,     LvhStr ("STR_NEW"),      juce::Colour (0xff2a2a38));
        configureButton (loadSetBtn_, LvhStr ("STR_OPEN_SET"), juce::Colour (0xff2a2a38));
        configureButton (saveSetBtn_, LvhStr ("STR_SAVE_SET"), juce::Colour (0xff2a2a38));
        configureButton (addBtn_,     LvhStr ("STR_ADD"),      juce::Colour (0xff1a3a1a));

        newBtn_.onClick     = [this] { if (onNewSetClicked)  onNewSetClicked(); };
        loadSetBtn_.onClick = [this] { if (onLoadSetClicked) onLoadSetClicked(); };
        saveSetBtn_.onClick = [this] { if (onSaveSetClicked) onSaveSetClicked(); };
        addBtn_.onClick     = [this] { if (onAddClicked)     onAddClicked(); };

        addAndMakeVisible (newBtn_);
        addAndMakeVisible (loadSetBtn_);
        addAndMakeVisible (saveSetBtn_);
        addAndMakeVisible (addBtn_);

        // ── Pin (Always on Top) button ────────────────────────────────
        pinBtn_ = std::make_unique<IconButton> ("Always on Top", Icons::pin);
        pinBtn_->setClickingTogglesState (true);
        pinBtn_->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff252535));
        pinBtn_->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffaa6600));
        pinBtn_->setTooltip (LvhStr ("STR_PIN_TOOLTIP"));
        pinBtn_->onClick = [this] {
            if (onPinToggled) onPinToggled (pinBtn_->getToggleState());
        };
        addAndMakeVisible (*pinBtn_);

        // ── Set name label (double-click to edit) ────────────────────
        setNameLabel_.setFont (stageFont (13.f, true));
        setNameLabel_.setColour (juce::Label::textColourId,
                                 juce::Colours::white);
        setNameLabel_.setColour (juce::Label::backgroundColourId,
                                 juce::Colour (0xff2a2a3c));
        setNameLabel_.setColour (juce::Label::backgroundWhenEditingColourId,
                                 juce::Colour (0xff3a3a50));
        setNameLabel_.setEditable (false, true);   // double-click to edit
        setNameLabel_.setJustificationType (juce::Justification::centredLeft);
        setNameLabel_.addListener (this);
        addAndMakeVisible (setNameLabel_);

        // ── Status label (file path + dirty marker) ──────────────────
        statusLabel_.setJustificationType (juce::Justification::centredLeft);
        statusLabel_.setColour (juce::Label::textColourId, juce::Colour (0xff888899));
        statusLabel_.setFont (stageFont (11.f));
        addAndMakeVisible (statusLabel_);

        // ── List + viewport ──────────────────────────────────────────
        listComponent_.onItemLoad     = [this] (int idx) {
            if (onItemLoad) onItemLoad (idx);
        };
        listComponent_.onItemSelected = [this] (int idx) {
            selectedIndex_ = idx;
            refreshList();
        };
        listComponent_.onItemMove = [this] (int from, int to) {
            manager_.moveItem (from, to);
            selectedIndex_ = to;
        };
        listComponent_.onItemRename = [this] (int idx) { showRenameDialog (idx); };
        listComponent_.onItemDelete = [this] (int idx) { confirmDelete     (idx); };

        viewport_.setViewedComponent (&listComponent_, false);
        viewport_.setScrollBarsShown (true, false);
        viewport_.setScrollBarThickness (8);
        addAndMakeVisible (viewport_);

        refresh();
    }

    void refresh()
    {
        setNameLabel_.setText (manager_.getSetName(), juce::dontSendNotification);
        refreshList();

        juce::String status;
        if (manager_.getCurrentFile().existsAsFile())
            status = manager_.getCurrentFile().getFullPathName();
        if (manager_.isDirty())
            status += (status.isEmpty() ? "" : "  ") + juce::String ("*");
        statusLabel_.setText (status, juce::dontSendNotification);
    }

    void refreshLanguage()
    {
        newBtn_.setButtonText     (LvhStr ("STR_NEW"));
        loadSetBtn_.setButtonText (LvhStr ("STR_OPEN_SET"));
        saveSetBtn_.setButtonText (LvhStr ("STR_SAVE_SET"));
        addBtn_.setButtonText     (LvhStr ("STR_ADD"));
        pinBtn_->setTooltip       (LvhStr ("STR_PIN_TOOLTIP"));
        refreshList();   // recreates SongStrips with updated LOAD button text
    }

    void setPinState (bool pinned)
    {
        pinBtn_->setToggleState (pinned, juce::dontSendNotification);
    }

    void resized() override
    {
        auto b = getLocalBounds();

        // Row 1: toolbar buttons (36 px)
        auto toolbar = b.removeFromTop (36);
        toolbar.reduce (6, 4);
        pinBtn_->setBounds    (toolbar.removeFromRight (28).reduced (0, 2));
        newBtn_.setBounds     (toolbar.removeFromLeft (60).reduced (2, 0));
        loadSetBtn_.setBounds (toolbar.removeFromLeft (80).reduced (2, 0));
        saveSetBtn_.setBounds (toolbar.removeFromLeft (80).reduced (2, 0));
        addBtn_.setBounds     (toolbar.removeFromLeft (70).reduced (2, 0));

        // Row 2: set name editor (30 px)
        setNameLabel_.setBounds (b.removeFromTop (30).reduced (6, 3));

        // Bottom: status
        statusLabel_.setBounds (b.removeFromBottom (22).reduced (8, 2));

        // Remaining: scrollable list
        viewport_.setBounds (b);
        listComponent_.setSize (viewport_.getMaximumVisibleWidth(),
                                juce::jmax (1, manager_.getItems().size() * SongStrip::kHeight));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff12121c));

        // Header background (toolbar + name row = 66 px)
        g.setColour (juce::Colour (0xff1a1a28));
        g.fillRect (0, 0, getWidth(), 66);

        // Separator line
        g.setColour (juce::Colour (0xff22cc44).withAlpha (0.4f));
        g.drawHorizontalLine (66, 0.f, (float) getWidth());
    }

    void setStatus (const juce::String& msg)
    {
        statusLabel_.setText (msg, juce::dontSendNotification);
    }

    // ── Keyboard / MIDI navigation ─────────────────────────────────────
    void moveSelection (int delta)
    {
        int n = manager_.getItems().size();
        if (n == 0) return;
        int newSel = juce::jlimit (0, n - 1,
                                   selectedIndex_ < 0 ? 0 : selectedIndex_ + delta);
        if (newSel != selectedIndex_)
        {
            selectedIndex_ = newSel;
            refreshList();
        }
    }

    // Navigate to an absolute index (used by MIDI Program Change).
    void navigateTo (int index)
    {
        int n = manager_.getItems().size();
        if (n == 0) return;
        int clamped = juce::jlimit (0, n - 1, index);
        if (clamped != selectedIndex_)
        {
            selectedIndex_ = clamped;
            refreshList();
        }
    }

    void loadSelected()
    {
        if (selectedIndex_ >= 0 && selectedIndex_ < manager_.getItems().size())
            if (onItemLoad) onItemLoad (selectedIndex_);
    }

private:
    // juce::Label::Listener
    void labelTextChanged (juce::Label* label) override
    {
        if (label == &setNameLabel_)
        {
            auto newName = setNameLabel_.getText().trim();
            if (newName.isNotEmpty() && newName != manager_.getSetName())
                manager_.setSetName (newName);
        }
    }

    void refreshList()
    {
        if (selectedIndex_ >= manager_.getItems().size())
            selectedIndex_ = -1;

        listComponent_.refresh (manager_.getItems(), manager_.getActiveIndex(), selectedIndex_);
        listComponent_.setSize (viewport_.getMaximumVisibleWidth(),
                                juce::jmax (1, manager_.getItems().size() * SongStrip::kHeight));
        resized();
    }

    void showRenameDialog (int index)
    {
        if (index < 0 || index >= manager_.getItems().size()) return;
        juce::String currentAlias = manager_.getItems()[index].alias;

        auto* dialog = new juce::AlertWindow (LvhStr ("STR_RENAME_ALIAS_TITLE"),
                                              LvhStr ("STR_RENAME_ALIAS_MSG"),
                                              juce::AlertWindow::NoIcon);
        dialog->addTextEditor ("alias", currentAlias);
        dialog->addButton (LvhStr ("STR_OK"),     1, juce::KeyPress (juce::KeyPress::returnKey));
        dialog->addButton (LvhStr ("STR_CANCEL"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        dialog->enterModalState (true,
            juce::ModalCallbackFunction::create ([this, index, dialog] (int result)
            {
                if (result == 1)
                {
                    auto newAlias = dialog->getTextEditorContents ("alias").trim();
                    if (newAlias.isNotEmpty())
                        manager_.renameItem (index, newAlias);
                }
            }), true);
    }

    void confirmDelete (int index)
    {
        if (index < 0 || index >= manager_.getItems().size()) return;
        juce::String alias = manager_.getItems()[index].alias;

        juce::NativeMessageBox::showYesNoBox (
            juce::MessageBoxIconType::QuestionIcon,
            LvhStr ("STR_DELETE_TITLE"),
            LvhStr ("STR_DELETE_CONFIRM_PRE") + alias + LvhStr ("STR_DELETE_CONFIRM_POST"),
            nullptr,
            juce::ModalCallbackFunction::create ([this, index] (int result)
            {
                if (result == 1)
                {
                    if (selectedIndex_ == index) selectedIndex_ = -1;
                    manager_.removeItem (index);
                }
            }));
    }

    void configureButton (juce::TextButton& btn, const juce::String& text, juce::Colour bg)
    {
        btn.setButtonText (text);
        btn.setColour (juce::TextButton::buttonColourId,  bg);
        btn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    }

    StageManager&                    manager_;
    juce::TextButton                 newBtn_, loadSetBtn_, saveSetBtn_, addBtn_;
    std::unique_ptr<IconButton>      pinBtn_;
    juce::Label                      setNameLabel_;
    juce::Label                      statusLabel_;
    juce::Viewport                   viewport_;
    StageListComponent               listComponent_;
    int                              selectedIndex_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageContentComponent)
};

// =====================================================================
// StageWindow — top-level DocumentWindow for Stage Performance Mode
//
// Title stays in sync with StageManager::getSetName() via refresh().
// =====================================================================
class StageWindow : public juce::DocumentWindow
{
public:
    std::function<void()> onClose;

    StageWindow (const juce::String& /*name*/, StageManager& manager)
        : juce::DocumentWindow ("Stage - " + manager.getSetName(),
                                juce::Colour (0xff12121c),
                                juce::DocumentWindow::allButtons),
          manager_ (manager)
    {
        setUsingNativeTitleBar (true);
        content_ = std::make_unique<StageContentComponent> (manager);
        content_->onPinToggled = [this] (bool pinned) {
            setAlwaysOnTop (pinned);
        };
        setContentNonOwned (content_.get(), true);
        setResizable (true, false);
        setResizeLimits (460, 340, 2560, 1440);
        centreWithSize (560, 520);
        setVisible (true);
    }

    void setPinState (bool pinned)
    {
        setAlwaysOnTop (pinned);
        if (content_) content_->setPinState (pinned);
    }

    void refreshLanguage()
    {
        if (content_) content_->refreshLanguage();
    }

    void closeButtonPressed() override
    {
        if (onClose) onClose();
        else setVisible (false);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (content_ != nullptr)
        {
            if (key == juce::KeyPress::upKey)    { content_->moveSelection (-1); return true; }
            if (key == juce::KeyPress::downKey)  { content_->moveSelection (+1); return true; }
            if (key == juce::KeyPress::returnKey){ content_->loadSelected();      return true; }
        }
        return juce::DocumentWindow::keyPressed (key);
    }

    void setOnItemLoad (std::function<void(int)> fn) { content_->onItemLoad       = std::move (fn); }
    void setOnAdd      (std::function<void()>    fn) { content_->onAddClicked     = std::move (fn); }
    void setOnSaveSet  (std::function<void()>    fn) { content_->onSaveSetClicked = std::move (fn); }
    void setOnLoadSet  (std::function<void()>    fn) { content_->onLoadSetClicked = std::move (fn); }
    void setOnNewSet   (std::function<void()>    fn) { content_->onNewSetClicked  = std::move (fn); }

    // ── MIDI remote control (called from UIManager on message thread) ──
    void remoteMoveSelection (int delta) { if (content_) content_->moveSelection (delta); }
    void remoteNavigateTo    (int index) { if (content_) content_->navigateTo    (index); }
    void remoteLoad          ()          { if (content_) content_->loadSelected  ();      }

    void refresh()
    {
        if (content_) content_->refresh();
        juce::String title = "Stage - " + manager_.getSetName();
        if (manager_.isDirty()) title += " *";
        setName (title);
    }

    void setStatus (const juce::String& msg) { if (content_) content_->setStatus (msg); }

private:
    StageManager&                          manager_;
    std::unique_ptr<StageContentComponent> content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageWindow)
};
