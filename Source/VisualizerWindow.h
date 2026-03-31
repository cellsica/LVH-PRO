#pragma once
#include <JuceHeader.h>
#include "Core/VisualizerManager.h"

// =============================================================================
// VisualizerWindow
//
// タイトルバーなし・細枠常時表示・ホバー時に左上に×ボタンを表示する
// フローティングビジュアライザーウィンドウ。
//
// 操作:
//   ドラッグ   — コンテンツ領域をドラッグしてウィンドウを移動
//   ×ボタン   — マウスオーバー時に左上に表示、クリックで閉じる
//   リサイズ   — OS フレーム端のドラッグで自由にリサイズ可能
// =============================================================================
class VisualizerWindow final : public juce::DocumentWindow
{
public:
    explicit VisualizerWindow (VisualizerManager& manager)
        : juce::DocumentWindow ("LVH Visualizer",
                                juce::Colour (0xff0a0a14),
                                0),   // built-in ボタンなし
          manager_ (manager)
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (0);
        setResizable (true, false);   // OS フレームでリサイズ可、隅コンポーネントなし

        renderView_ = std::make_unique<RenderView> (manager_, [this] {
            setVisible (false);
            if (onClose) onClose();
        });
        setContentOwned (renderView_.get(), false);

        auto preferred = manager_.getPreferredSize();
        setSize (preferred.getWidth(), preferred.getHeight());
    }

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClose) onClose();
    }

    // ウィンドウを閉じた時のコールバック（ツールバーボタン状態の同期用）
    std::function<void()> onClose;

    // =========================================================================
    // RenderView — OpenGL バックエンド描画 + ホバー UI
    // =========================================================================
    class RenderView final : public juce::Component,
                             private juce::Timer
    {
    public:
        RenderView (VisualizerManager& mgr, std::function<void()> closeCallback)
            : manager_ (mgr), closeCallback_ (std::move (closeCallback))
        {
            setOpaque (true);
            openGLContext_.attachTo (*this);
            startTimerHz (60);
        }

        ~RenderView() override
        {
            stopTimer();
            openGLContext_.detach();
        }

    private:
        // ── 定数 ─────────────────────────────────────────────────────────
        static constexpr int kBtnSize   = 16;  // × ボタンの直径
        static constexpr int kBtnMargin =  6;  // 左上からのマージン
        static constexpr int kBorderPx  =  1;  // 枠線の太さ

        juce::Rectangle<int> closeBtnBounds() const noexcept
        {
            return { kBtnMargin, kBtnMargin, kBtnSize, kBtnSize };
        }

        // ── Paint ─────────────────────────────────────────────────────────
        void paint (juce::Graphics& g) override
        {
            // 1. ビジュアライザー描画
            manager_.render (g, &openGLContext_);

            // 2. 細枠（常時表示）
            g.setColour (juce::Colour (0x88445566));
            g.drawRect (getLocalBounds(), kBorderPx);

            // 3. × ボタン（マウスオーバー時のみ）
            if (mouseOver_)
            {
                auto cb = closeBtnBounds().toFloat();

                // 背景サークル（ホバー時は少し明るく）
                g.setColour (mouseOnClose_
                    ? juce::Colour (0xddcc3333)
                    : juce::Colour (0xaa1e1e2e));
                g.fillEllipse (cb);

                // × のライン
                g.setColour (juce::Colours::white.withAlpha (0.80f));
                const float pad = 4.5f;
                float x1 = cb.getX() + pad,  y1 = cb.getY() + pad;
                float x2 = cb.getRight() - pad, y2 = cb.getBottom() - pad;
                g.drawLine (x1, y1, x2, y2, 1.5f);
                g.drawLine (x2, y1, x1, y2, 1.5f);
            }
        }

        void timerCallback() override { repaint(); }

        // ── Mouse ─────────────────────────────────────────────────────────
        void mouseEnter (const juce::MouseEvent&) override
        {
            mouseOver_ = true;
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            mouseOver_   = false;
            mouseOnClose_ = false;
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            mouseOnClose_ = closeBtnBounds().contains (e.getPosition());
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            // × ボタン以外の領域はウィンドウドラッグ
            if (! closeBtnBounds().contains (e.getPosition()))
                if (auto* w = getTopLevelComponent())
                    dragger_.startDraggingComponent (w, e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! closeBtnBounds().contains (e.getMouseDownPosition()))
                if (auto* w = getTopLevelComponent())
                    dragger_.dragComponent (w, e, nullptr);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (closeBtnBounds().contains (e.getPosition()))
                if (closeCallback_) closeCallback_();
        }

        // ── State ─────────────────────────────────────────────────────────
        VisualizerManager&     manager_;
        std::function<void()>  closeCallback_;
        juce::OpenGLContext    openGLContext_;
        juce::ComponentDragger dragger_;
        bool mouseOver_   = false;
        bool mouseOnClose_ = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderView)
    };

private:
    VisualizerManager&          manager_;
    std::unique_ptr<RenderView> renderView_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizerWindow)
};
