// VUMeterWindow.cpp — Win32 platform implementation
// windows.h must be included BEFORE JUCE headers in this TU to avoid
// wingdi.h / juce::Rectangle name clash in other translation units.
#if defined(_WIN32) || defined(_WIN64)
 #include <windows.h>
#endif

#include "VUMeterWindow.h"

// =========================================================================
// Win32 WndProc subclass (file-scope, not a class member)
// Strategy: call the original JUCE proc first to get the baseline hit-test
// result, then override only HTCLIENT responses for the face area.
// This ensures the native title bar (HTCAPTION / HTCLOSE / HTSYSMENU)
// continues to work normally — only the client area becomes click-through.
// =========================================================================
#if JUCE_WINDOWS

static LRESULT CALLBACK vuMeterSubclassProc (HWND hwnd, UINT msg,
                                              WPARAM wp, LPARAM lp)
{
    auto* original = (WNDPROC) GetProp (hwnd, "VUMeterOriginalProc");

    if (msg == WM_NCHITTEST)
    {
        // Ask the original JUCE WndProc for its opinion first.
        LRESULT base = original ? CallWindowProc (original, hwnd, msg, wp, lp)
                                : DefWindowProc  (hwnd, msg, wp, lp);

        // Leave non-client hits (title bar, close button, border, …) untouched.
        if (base != HTCLIENT) return base;

        // For the client area, apply click-through except the handle strip.
        const int cursorY = (short) HIWORD (lp);   // screen Y, signed
        RECT rc;
        GetWindowRect (hwnd, &rc);

        // Bottom kHandleH px strip → interactive (drag / right-click)
        if (cursorY >= rc.bottom - VUMeterComponent::kHandleH)
            return HTCLIENT;

        return HTTRANSPARENT;   // face area → click-through
    }

    return original ? CallWindowProc (original, hwnd, msg, wp, lp)
                    : DefWindowProc  (hwnd, msg, wp, lp);
}

#endif   // JUCE_WINDOWS

// =========================================================================
// VUMeterWindow — Win32 style application (idempotent)
// =========================================================================

void VUMeterWindow::applyWin32Styles()
{
#if JUCE_WINDOWS
    if (nativeHwnd_ != nullptr) return;   // already installed

    auto* peer = getPeer();
    if (peer == nullptr) return;

    auto* hwnd = (HWND) peer->getNativeHandle();
    nativeHwnd_ = hwnd;

    // Subclass WndProc for NCHITTEST click-through;
    // Opacity is managed by setOpacity() / Component::setAlpha() — no manual call needed.
    // store original proc as a window property (avoids class member access).
    LONG_PTR original = SetWindowLongPtr (hwnd, GWLP_WNDPROC,
                                           (LONG_PTR) vuMeterSubclassProc);
    SetProp (hwnd, "VUMeterOriginalProc", (HANDLE) original);
#endif
}

void VUMeterWindow::setOpacity (float opacity)
{
    opacity_ = juce::jlimit (0.0f, 1.0f, opacity);

    // setAlpha() updates componentTransparency (used by JUCE when creating the peer).
    // For alpha < 1.0 it also calls peer->setAlpha() which sets updateLayeredWindowAlpha
    // and triggers a repaint → UpdateLayeredWindow applies the new alpha.
    setAlpha (opacity_);

    // For alpha == 1.0, Component::setAlpha() skips peer->setAlpha() because the
    // component is no longer "transparent". We call it explicitly to reset
    // updateLayeredWindowAlpha to 255 (fully opaque).
    if (opacity_ >= 1.0f)
        if (auto* peer = getPeer())
            peer->setAlpha (1.0f);
}

void VUMeterWindow::uninstallWndProc()
{
#if JUCE_WINDOWS
    if (nativeHwnd_ == nullptr) return;
    auto* hwnd = (HWND) nativeHwnd_;

    auto* original = (WNDPROC) GetProp (hwnd, "VUMeterOriginalProc");
    if (original)
    {
        SetWindowLongPtr (hwnd, GWLP_WNDPROC, (LONG_PTR) original);
        RemoveProp (hwnd, "VUMeterOriginalProc");
    }
    nativeHwnd_ = nullptr;
#endif
}
