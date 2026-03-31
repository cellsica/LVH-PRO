// VisualizerWindow.cpp — Win32 platform implementation (opacity + click-through)
// windows.h must be included BEFORE JUCE headers in this TU to avoid
// wingdi.h / juce::Rectangle name clash in other translation units.
#if defined(_WIN32) || defined(_WIN64)
 #include <windows.h>
#endif

#include "VisualizerWindow.h"

// =========================================================================
// Win32 WndProc subclass
//
// Strategy (identical to VUMeterWindow):
//   1. Call original JUCE proc first to get the baseline hit-test result.
//   2. Leave non-client hits (title bar, border) untouched.
//   3. For the client area, check the "VizClickThrough" window property:
//        false → normal HTCLIENT (interactive window)
//        true  → HTTRANSPARENT everywhere except the close-button corner
// =========================================================================
#if JUCE_WINDOWS

// Close-button area size (must match RenderView constants)
static constexpr int kCloseBtnSize   = 16;
static constexpr int kCloseBtnMargin =  6;

static LRESULT CALLBACK vizSubclassProc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* original = (WNDPROC) GetProp (hwnd, "VizOriginalProc");

    if (msg == WM_NCHITTEST)
    {
        // Ask the original JUCE WndProc for its opinion first.
        LRESULT base = original ? CallWindowProc (original, hwnd, msg, wp, lp)
                                : DefWindowProc  (hwnd, msg, wp, lp);

        // Leave non-client hits (border, caption, …) untouched.
        if (base != HTCLIENT) return base;

        // Check click-through flag stored as a window property.
        if (! GetProp (hwnd, "VizClickThrough"))
            return HTCLIENT;

        // Click-through mode: allow close-button corner to remain interactive.
        const int cursorX = (short) LOWORD (lp);
        const int cursorY = (short) HIWORD (lp);
        RECT rc;
        GetWindowRect (hwnd, &rc);

        const int localX = cursorX - rc.left;
        const int localY = cursorY - rc.top;
        const bool onCloseBtn = (localX >= kCloseBtnMargin
                              && localX <= kCloseBtnMargin + kCloseBtnSize
                              && localY >= kCloseBtnMargin
                              && localY <= kCloseBtnMargin + kCloseBtnSize);

        return onCloseBtn ? HTCLIENT : HTTRANSPARENT;
    }

    return original ? CallWindowProc (original, hwnd, msg, wp, lp)
                    : DefWindowProc  (hwnd, msg, wp, lp);
}

#endif  // JUCE_WINDOWS

// =========================================================================
// VisualizerWindow — Win32 style application
// =========================================================================

void VisualizerWindow::applyWin32Styles()
{
#if JUCE_WINDOWS
    if (nativeHwnd_ != nullptr) return;   // idempotent

    auto* peer = getPeer();
    if (peer == nullptr) return;

    auto* hwnd = (HWND) peer->getNativeHandle();
    nativeHwnd_ = hwnd;

    // Subclass WndProc for click-through support (WM_NCHITTEST override).
    LONG_PTR original = SetWindowLongPtr (hwnd, GWLP_WNDPROC,
                                           (LONG_PTR) vizSubclassProc);
    SetProp (hwnd, "VizOriginalProc",   (HANDLE) original);
    SetProp (hwnd, "VizClickThrough",   (HANDLE) (UINT_PTR) (clickThrough_ ? 1 : 0));

    // Apply any opacity that was set before the handle was available.
    if (opacity_ < 1.0f)
        setOpacity (opacity_);
#endif
}

void VisualizerWindow::uninstallWndProc()
{
#if JUCE_WINDOWS
    if (nativeHwnd_ == nullptr) return;

    auto* hwnd = (HWND) nativeHwnd_;
    auto* original = (WNDPROC) GetProp (hwnd, "VizOriginalProc");
    if (original)
    {
        SetWindowLongPtr (hwnd, GWLP_WNDPROC, (LONG_PTR) original);
        RemoveProp (hwnd, "VizOriginalProc");
        RemoveProp (hwnd, "VizClickThrough");
    }
    nativeHwnd_ = nullptr;
#endif
}

void VisualizerWindow::setOpacity (float opacity)
{
    opacity_ = juce::jlimit (0.0f, 1.0f, opacity);

    // Component::setAlpha() → peer->setAlpha():
    //   • isLayered == false (no windowIsSemiTransparent flag)
    //     → adds WS_EX_LAYERED + calls SetLayeredWindowAttributes(LWA_ALPHA)
    //   • LWA_ALPHA = uniform alpha, compatible with OpenGL rendering on Win10/11
    setAlpha (opacity_);

    // For alpha == 1.0, Component::setAlpha() skips peer->setAlpha().
    // Call it explicitly to reset LWA to 255 (fully opaque).
    if (opacity_ >= 1.0f)
        if (auto* peer = getPeer())
            peer->setAlpha (1.0f);
}

void VisualizerWindow::setClickThrough (bool enabled)
{
    clickThrough_ = enabled;
#if JUCE_WINDOWS
    if (nativeHwnd_ != nullptr)
        SetProp ((HWND) nativeHwnd_, "VizClickThrough",
                 (HANDLE) (UINT_PTR) (enabled ? 1 : 0));
#endif
}
