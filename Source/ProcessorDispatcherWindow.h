#pragma once
#include <JuceHeader.h>
#include "Core/ThemePalette.h"
#include "UiCommon.h"

// Forward declaration
class UIManager;

// =============================================================================
// ProcessorDispatcherWindow
//
// Compact floating window that lists all discovered processor DLLs and lets
// the user start/stop each one individually.
//
// Each row shows:
//   [accent swatch]  [plugin name]  [START / STOP button]
//
// The window queries UIManager for the processor list and delegates
// start/stop actions back to it.
// =============================================================================
class ProcessorDispatcherWindow : public juce::DocumentWindow
{
public:
    // -------------------------------------------------------------------------
    // Content component — the actual list UI
    // -------------------------------------------------------------------------
    class Content : public juce::Component
    {
    public:
        static constexpr int kRowH   = 40;
        static constexpr int kPad    =  8;
        static constexpr int kSwatchW = 8;
        static constexpr int kBtnW   = 64;

        explicit Content (UIManager& ui) : ui_ (ui)
        {
            rebuild();
        }

        // Rebuild the list of rows from the current UIManager state.
        void rebuild()
        {
            rows_.clear();
            removeAllChildren();

            int n = ui_.getNumDiscoveredProcessors();
            for (int i = 0; i < n; ++i)
            {
                auto row = std::make_unique<Row> (i, ui_);
                addAndMakeVisible (*row);
                rows_.push_back (std::move (row));
            }

            if (n == 0)
            {
                emptyLabel_ = std::make_unique<juce::Label>();
                emptyLabel_->setText ("No processors found in\nProcessors/ directory.",
                                      juce::dontSendNotification);
                emptyLabel_->setJustificationType (juce::Justification::centred);
                emptyLabel_->setColour (juce::Label::textColourId,
                                        ThemePalette::get (ColourId::TextSecondary));
                addAndMakeVisible (*emptyLabel_);
            }

            int totalH = n > 0 ? (n * kRowH + kPad * 2) : 80;
            setSize (280, totalH);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (kPad);
            if (rows_.empty() && emptyLabel_ != nullptr)
            {
                emptyLabel_->setBounds (area);
                return;
            }
            for (auto& row : rows_)
            {
                row->setBounds (area.removeFromTop (kRowH).reduced (0, 3));
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (ThemePalette::get (ColourId::BgPanel));
        }

    private:
        // ------------------------------------------------------------------
        // Row — one processor entry
        // ------------------------------------------------------------------
        class Row : public juce::Component
        {
        public:
            Row (int index, UIManager& ui) : index_ (index), ui_ (ui)
            {
                // Start / Stop button
                btn_.setClickingTogglesState (false);
                btn_.setColour (juce::TextButton::buttonColourId,
                                ThemePalette::get (ColourId::AccentGreen));
                btn_.setColour (juce::TextButton::textColourOffId,
                                ThemePalette::get (ColourId::BgPanel));
                btn_.onClick = [this] { onButtonClick(); };
                addAndMakeVisible (btn_);

                updateState();
            }

            void updateState()
            {
                bool running = ui_.isProcessorRunning (index_);
                btn_.setButtonText (running ? "STOP" : "START");
                btn_.setColour (juce::TextButton::buttonColourId,
                                running ? ThemePalette::get (ColourId::StateBypass)
                                        : ThemePalette::get (ColourId::AccentGreen));
                repaint();
            }

            void resized() override
            {
                auto area = getLocalBounds();
                btn_.setBounds (area.removeFromRight (Content::kBtnW).reduced (2));
                area.removeFromLeft (Content::kSwatchW + Content::kPad);  // swatch + gap
                // remaining area is the name label — painted in paint()
            }

            void paint (juce::Graphics& g) override
            {
                auto area = getLocalBounds();

                // Background
                g.setColour (ThemePalette::get (ColourId::BgPanelAlt));
                g.fillRoundedRectangle (area.toFloat(), 4.f);

                // Accent colour swatch
                auto swatchArea = area.removeFromLeft (Content::kSwatchW);
                juce::Colour accent (ui_.getProcessorAccentColour (index_));
                g.setColour (accent);
                g.fillRoundedRectangle (swatchArea.toFloat().reduced (0.f, 2.f), 3.f);

                // Plugin name
                area.removeFromLeft (Content::kPad);
                area.removeFromRight (Content::kBtnW + 4);
                g.setColour (ThemePalette::get (ColourId::TextPrimary));
                g.setFont (juce::Font (14.f));
                g.drawText (ui_.getProcessorName (index_), area,
                            juce::Justification::centredLeft, true);
            }

        private:
            void onButtonClick()
            {
                if (ui_.isProcessorRunning (index_))
                    ui_.stopProcessor (index_);
                else
                    ui_.startProcessor (index_);

                // Notify the parent Content to refresh all rows
                if (auto* content = findParentComponentOfClass<Content>())
                    content->refreshStates();
            }

            int        index_;
            UIManager& ui_;
            juce::TextButton btn_;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Row)
        };

        // Refresh button labels/colours without rebuilding the whole list.
        void refreshStates()
        {
            for (auto& row : rows_)
                row->updateState();
            repaint();
        }

        UIManager& ui_;
        std::vector<std::unique_ptr<Row>> rows_;
        std::unique_ptr<juce::Label>      emptyLabel_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

    // -------------------------------------------------------------------------
    // Window
    // -------------------------------------------------------------------------
    explicit ProcessorDispatcherWindow (UIManager& ui)
        : juce::DocumentWindow ("Processor Manager",
                                ThemePalette::get (ColourId::BgPrimary),
                                juce::DocumentWindow::closeButton),
          content_ (std::make_unique<Content> (ui))
    {
        setUsingNativeTitleBar (false);
        setResizable (false, false);
        // Use false for resizeToFit to avoid a feedback loop where moving the
        // window triggers resized() → content setBounds → window grows by one
        // item height each time.  Instead, size the window explicitly.
        setContentNonOwned (content_.get(), false);
        centreWithSize (content_->getWidth(),
                        content_->getHeight() + getTitleBarHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (onCloseRequest)
            onCloseRequest();
    }

    // Rebuild the content list (call after scan or plugin changes).
    void refresh()
    {
        content_->rebuild();
        setContentNonOwned (content_.get(), false);
        setSize (content_->getWidth(),
                 content_->getHeight() + getTitleBarHeight());
    }

    std::function<void()> onCloseRequest;

private:
    std::unique_ptr<Content> content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorDispatcherWindow)
};
