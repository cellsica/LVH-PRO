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
//   ドラッグ       — コンテンツ領域をドラッグしてウィンドウを移動
//   ×ボタン       — マウスオーバー時に左上に表示、クリックで閉じる
//   リサイズ       — OS フレーム端のドラッグで自由にリサイズ可能
//   右クリック     — プラグイン選択 / Auto-Switching / 透過 / クリック透過設定
//   N キー         — 次のプラグインへ手動切り替え
//   F12            — 全画面 ↔ ウィンドウ表示トグル
//
// Phase E additions:
//   - Window Opacity (right-click menu: 100% / 75% / 50%)
//   - Rescan DLLs (right-click menu)
//   - setOpacity() for UIManager integration
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
        setResizable (true, false);

        renderView_ = std::make_unique<RenderView> (manager_, [this] {
            setVisible (false);
            if (onClose) onClose();
        });
        setContentOwned (renderView_.get(), false);

        auto preferred = manager_.getPreferredSize();
        setSize (preferred.getWidth(), preferred.getHeight());
    }

    ~VisualizerWindow() override = default;

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClose) onClose();
    }

    /** Show the window and install Win32 styles (idempotent). */
    void show()
    {
        setVisible (true);
        toFront (true);
        applyWin32Styles();   // installs WndProc subclass once the peer exists
    }

    // ── Opacity (Phase E) ─────────────────────────────────────────────────────

    /** Set the window opacity [0.0, 1.0].
        Uses SetLayeredWindowAttributes (LWA_ALPHA) which is compatible with
        OpenGL rendering, unlike per-pixel alpha (UpdateLayeredWindow). */
    void setOpacity (float opacity);

    float getOpacity() const noexcept { return opacity_; }

    // ウィンドウを閉じた時のコールバック（ツールバーボタン状態の同期用）
    std::function<void()> onClose;

    // =========================================================================
    // RenderView — OpenGL バックエンド描画 + ホバー UI + 右クリックメニュー
    // =========================================================================
    class RenderView final : public juce::Component,
                             private juce::Timer
    {
    public:
        RenderView (VisualizerManager& mgr, std::function<void()> closeCallback)
            : manager_ (mgr), closeCallback_ (std::move (closeCallback))
        {
            setOpaque (true);
            setWantsKeyboardFocus (true);
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
        static constexpr int   kBtnSize    = 16;
        static constexpr int   kBtnMargin  =  6;
        static constexpr int   kBorderPx   =  1;
        static constexpr float kFlashDecay = 0.06f;

        juce::Rectangle<int> closeBtnBounds() const noexcept
        {
            return { kBtnMargin, kBtnMargin, kBtnSize, kBtnSize };
        }

        void triggerFlash() { flashAlpha_ = 0.55f; }

        // ── Paint ─────────────────────────────────────────────────────────
        void paint (juce::Graphics& g) override
        {
            int current = manager_.getCurrentPluginIndex();
            if (current != lastRenderedIndex_)
            {
                lastRenderedIndex_ = current;
                triggerFlash();
            }

            manager_.render (g, &openGLContext_);

            if (flashAlpha_ > 0.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (flashAlpha_));
                g.fillAll();
                flashAlpha_ = juce::jmax (0.0f, flashAlpha_ - kFlashDecay);
            }

            g.setColour (juce::Colour (0x88445566));
            g.drawRect (getLocalBounds(), kBorderPx);

            if (mouseOver_)
            {
                auto cb = closeBtnBounds().toFloat();
                g.setColour (mouseOnClose_
                    ? juce::Colour (0xddcc3333)
                    : juce::Colour (0xaa1e1e2e));
                g.fillEllipse (cb);

                g.setColour (juce::Colours::white.withAlpha (0.80f));
                const float pad = 4.5f;
                float x1 = cb.getX() + pad,    y1 = cb.getY() + pad;
                float x2 = cb.getRight() - pad, y2 = cb.getBottom() - pad;
                g.drawLine (x1, y1, x2, y2, 1.5f);
                g.drawLine (x2, y1, x1, y2, 1.5f);
            }
        }

        void timerCallback() override { repaint(); }

        // ── Keyboard ──────────────────────────────────────────────────────
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.getKeyCode() == 'N' || key.getKeyCode() == 'n')
            {
                manager_.nextPlugin();
                return true;
            }

            if (key == juce::KeyPress::F12Key)
            {
                if (auto* dw = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
                {
                    dw->setFullScreen (! dw->isFullScreen());
                    return true;
                }
            }

            return false;
        }

        // ── Context menu ──────────────────────────────────────────────────
        void showContextMenu()
        {
            juce::PopupMenu menu;

            // ---- Visualizer List ----
            juce::PopupMenu vizMenu;
            auto names = manager_.getPluginNames();
            for (int i = 0; i < names.size(); ++i)
                vizMenu.addItem (1000 + i, names[i], true,
                                 i == manager_.getCurrentPluginIndex());
            if (names.isEmpty())
                vizMenu.addItem (1, "(No plugins loaded)", false, false);
            menu.addSubMenu ("Visualizer", vizMenu);
            menu.addSeparator();

            // ---- Auto-Switching ----
            juce::PopupMenu autoMenu;
            auto mode     = manager_.getSwitchMode();
            int  interval = manager_.getSwitchInterval();
            autoMenu.addItem (2000, "Off",    true, mode == VisualizerManager::SwitchMode::Manual);
            autoMenu.addItem (2001, "15 sec", true, mode == VisualizerManager::SwitchMode::Sequential && interval == 15);
            autoMenu.addItem (2002, "30 sec", true, mode == VisualizerManager::SwitchMode::Sequential && interval == 30);
            autoMenu.addItem (2003, "1 min",  true, mode == VisualizerManager::SwitchMode::Sequential && interval == 60);
            autoMenu.addSeparator();
            autoMenu.addItem (2004, "Random", true, mode == VisualizerManager::SwitchMode::Random);
            menu.addSubMenu ("Auto-Switching", autoMenu);
            menu.addSeparator();

            // ---- Window Opacity ----
            auto* vw = dynamic_cast<VisualizerWindow*> (getTopLevelComponent());
            float curOpacity = vw ? vw->getOpacity() : 1.0f;
            juce::PopupMenu opacityMenu;
            opacityMenu.addItem (3001, "100%", true, curOpacity >= 0.99f);
            opacityMenu.addItem (3002,  "75%", true, curOpacity > 0.60f && curOpacity < 0.99f);
            opacityMenu.addItem (3003,  "50%", true, curOpacity <= 0.60f);
            menu.addSubMenu ("Window Opacity", opacityMenu);

            menu.addSeparator();

            // ---- Rescan ----
            menu.addItem (3000, "Rescan Plugins");

            menu.showMenuAsync (juce::PopupMenu::Options{},
                [this] (int result)
                {
                    auto* vw2 = dynamic_cast<VisualizerWindow*> (getTopLevelComponent());

                    if (result >= 1000 && result < 2000)
                    {
                        manager_.setCurrentPlugin (result - 1000);
                        return;
                    }

                    switch (result)
                    {
                        // Auto-switching
                        case 2000: manager_.setSwitchMode (VisualizerManager::SwitchMode::Manual);     break;
                        case 2001: manager_.setSwitchInterval (15);
                                   manager_.setSwitchMode (VisualizerManager::SwitchMode::Sequential); break;
                        case 2002: manager_.setSwitchInterval (30);
                                   manager_.setSwitchMode (VisualizerManager::SwitchMode::Sequential); break;
                        case 2003: manager_.setSwitchInterval (60);
                                   manager_.setSwitchMode (VisualizerManager::SwitchMode::Sequential); break;
                        case 2004: manager_.setSwitchMode (VisualizerManager::SwitchMode::Random);     break;

                        // Opacity
                        case 3001: if (vw2) vw2->setOpacity (1.00f); break;
                        case 3002: if (vw2) vw2->setOpacity (0.75f); break;
                        case 3003: if (vw2) vw2->setOpacity (0.50f); break;

                        // Rescan
                        case 3000: manager_.rescan(); break;

                        default: break;
                    }
                });
        }

        // ── Mouse ─────────────────────────────────────────────────────────
        void mouseEnter (const juce::MouseEvent&) override { mouseOver_ = true; }

        void mouseExit (const juce::MouseEvent&) override
        {
            mouseOver_    = false;
            mouseOnClose_ = false;
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            mouseOnClose_ = closeBtnBounds().contains (e.getPosition());
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isRightButtonDown())
            {
                grabKeyboardFocus();
                showContextMenu();
                return;
            }

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
        bool  mouseOver_         = false;
        bool  mouseOnClose_      = false;
        int   lastRenderedIndex_ = -1;
        float flashAlpha_        = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderView)
    };

private:
    void applyWin32Styles();    // implemented in VisualizerWindow.cpp

    VisualizerManager&          manager_;
    std::unique_ptr<RenderView> renderView_;
    void*  nativeHwnd_   = nullptr;
    float  opacity_      = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizerWindow)
};
