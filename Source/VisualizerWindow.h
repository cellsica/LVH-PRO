#pragma once
#include <JuceHeader.h>
#include "Core/VisualizerManager.h"

// =============================================================================
// VisualizerWindow  (Phase A — OpenGL integration base)
//
// A floating DocumentWindow that:
//   • Attaches a juce::OpenGLContext for hardware-accelerated rendering.
//   • Drives VisualizerManager::render() at ~60 fps via a Timer.
//   • Forwards the OpenGL context pointer to each plugin's render() call.
//
// Phase A establishes the plumbing.  Visual polish / layout options are
// deferred to later phases.
// =============================================================================
class VisualizerWindow final : public juce::DocumentWindow
{
public:
    explicit VisualizerWindow (VisualizerManager& manager)
        : juce::DocumentWindow ("LVH Visualizer",
                                juce::Colour (0xff0a0a14),
                                juce::DocumentWindow::closeButton),
          manager_ (manager)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);

        renderView_ = std::make_unique<RenderView> (manager_);
        setContentOwned (renderView_.get(), false);

        // プラグインの推奨サイズに合わせる（未ロード時は 500x500）
        auto preferred = manager_.getPreferredSize();
        setSize (preferred.getWidth(), preferred.getHeight());
        setAlwaysOnTop (false);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClose) onClose();
    }

    // Called when the user closes the window (sync toolbar button state).
    std::function<void()> onClose;

    // -------------------------------------------------------------------------
    // Inner component — hosts the OpenGL context and runs the render timer.
    // -------------------------------------------------------------------------
    class RenderView final : public juce::Component,
                             private juce::Timer
    {
    public:
        explicit RenderView (VisualizerManager& mgr) : manager_ (mgr)
        {
            setOpaque (true);

            // Attach OpenGL context — setComponentPaintingEnabled(true) is the
            // default and causes JUCE to route paint() through OpenGL, which is
            // exactly what we want for plugins that draw with juce::Graphics.
            openGLContext_.attachTo (*this);

            startTimerHz (60);
        }

        ~RenderView() override
        {
            stopTimer();
            openGLContext_.detach();
        }

        juce::OpenGLContext& getOpenGLContext() noexcept { return openGLContext_; }

    private:
        void paint (juce::Graphics& g) override
        {
            manager_.render (g, &openGLContext_);
        }

        void timerCallback() override
        {
            repaint();
        }

        VisualizerManager&   manager_;
        juce::OpenGLContext  openGLContext_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderView)
    };

private:
    VisualizerManager&          manager_;
    std::unique_ptr<RenderView> renderView_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizerWindow)
};
