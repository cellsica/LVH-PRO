#pragma once
#include <JuceHeader.h>
#include "VirtualLayoutComponent.h"
#include "../Core/ThemePalette.h"

/**
 * @file LayoutStudioWindow.h
 * @brief Floating window for Instrument Layout Studio Phase B.
 *
 * Contains a selector row (keyboard size / pad count) and a
 * VirtualLayoutComponent with real-time MIDI feedback.
 *
 * @note Mission 055 Phase B
 */
class LayoutStudioWindow : public juce::DocumentWindow
{
public:
    /** Called when the user closes the window (async-safe). */
    std::function<void()> onCloseRequest;

    // ── Construction ──────────────────────────────────────────────────────────
    LayoutStudioWindow()
        : juce::DocumentWindow ("Layout Studio",
                                ThemePalette::get (ColourId::BgPrimary),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        setResizable (false, false);
        content_ = std::make_unique<Content>();
        addAndMakeVisible (content_.get());
        updateWindowSize();
        setVisible (true);
    }

    // ── MIDI feedback ─────────────────────────────────────────────────────────
    /**
     * @brief Forward an incoming MIDI message to the virtual keyboard.
     * Thread-safe — may be called from the MIDI input thread.
     */
    void handleMidiMessage (const juce::MidiMessage& msg)
    {
        content_->layout.handleMidiMessage (msg);
    }

    // ── DocumentWindow overrides ──────────────────────────────────────────────
    void closeButtonPressed() override
    {
        if (onCloseRequest)
            juce::MessageManager::callAsync (onCloseRequest);
    }

    void resized() override
    {
        juce::DocumentWindow::resized();
        if (content_ != nullptr)
            content_->setBounds (0, getTitleBarHeight(), fixedW_, fixedH_);
    }

private:
    // ── Content component ─────────────────────────────────────────────────────
    struct Content : public juce::Component
    {
        // ── Sub-components ────────────────────────────────────────────────────
        juce::Label            keysLabel;
        juce::ComboBox         keysCombo;
        juce::Label            padsLabel;
        juce::ComboBox         padsCombo;
        VirtualLayoutComponent layout;

        Content()
        {
            // Keys label
            keysLabel.setText ("Keys:", juce::dontSendNotification);
            keysLabel.setColour (juce::Label::textColourId,
                                 ThemePalette::get (ColourId::TextSecondary));
            addAndMakeVisible (keysLabel);

            // Keys combo
            keysCombo.addItem ("25", 1);
            keysCombo.addItem ("49", 2);
            keysCombo.addItem ("61", 3);
            keysCombo.addItem ("88", 4);
            keysCombo.setSelectedId (VirtualLayoutComponent::kDefaultRangeIndex + 1,
                                     juce::dontSendNotification);
            keysCombo.onChange = [this] {
                layout.setRangeIndex (keysCombo.getSelectedId() - 1);
            };
            addAndMakeVisible (keysCombo);

            // Pads label
            padsLabel.setText ("Pads:", juce::dontSendNotification);
            padsLabel.setColour (juce::Label::textColourId,
                                 ThemePalette::get (ColourId::TextSecondary));
            addAndMakeVisible (padsLabel);

            // Pads combo
            padsCombo.addItem ("None", 1);
            padsCombo.addItem ("4",    2);
            padsCombo.addItem ("8",    3);
            padsCombo.addItem ("16",   4);
            padsCombo.setSelectedId (4, juce::dontSendNotification);  // 16 pads default
            padsCombo.onChange = [this] {
                const int counts[] = { 0, 4, 8, 16 };
                int idx = juce::jlimit (0, 3, padsCombo.getSelectedId() - 1);
                layout.setNumPads (counts[idx]);
                if (auto* w = dynamic_cast<LayoutStudioWindow*> (getTopLevelComponent()))
                    w->updateWindowSize();
            };
            addAndMakeVisible (padsCombo);

            addAndMakeVisible (layout);
            setSize (layout.preferredWidth(), kSelectorH + layout.preferredHeight());
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (ThemePalette::get (ColourId::BgPrimary));
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto row  = area.removeFromTop (kSelectorH).reduced (8, 4);

            keysLabel.setBounds (row.removeFromLeft (38));
            keysCombo.setBounds (row.removeFromLeft (60).reduced (0, 2));
            row.removeFromLeft (16);
            padsLabel.setBounds (row.removeFromLeft (38));
            padsCombo.setBounds (row.removeFromLeft (60).reduced (0, 2));

            layout.setBounds (area);
        }

        static constexpr int kSelectorH = 36;
    };

    // ── Size management ───────────────────────────────────────────────────────
    void updateWindowSize()
    {
        if (content_ == nullptr) return;
        fixedW_ = content_->layout.preferredWidth();
        fixedH_ = Content::kSelectorH + content_->layout.preferredHeight();
        content_->setSize (fixedW_, fixedH_);
        centreWithSize (fixedW_, fixedH_ + getTitleBarHeight());
    }

    // ── Members ───────────────────────────────────────────────────────────────
    std::unique_ptr<Content> content_;
    int fixedW_ = 700;
    int fixedH_ = 200;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayoutStudioWindow)
};
