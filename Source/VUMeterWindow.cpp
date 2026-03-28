// VUMeterWindow.cpp — Win32 platform implementation
// windows.h must be included BEFORE JUCE headers in this TU to avoid
// wingdi.h / juce::Rectangle name clash in other translation units.
#if defined(_WIN32) || defined(_WIN64)
 #include <windows.h>
#endif

#include "VUMeterWindow.h"

// =========================================================================
// Win32 WndProc subclass (file-scope, not a class member)
// =========================================================================
#if JUCE_WINDOWS

static LRESULT CALLBACK vuMeterSubclassProc (HWND hwnd, UINT msg,
                                              WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST)
    {
        // Screen-space coordinates — signed cast handles negative multi-monitor coords
        const int cursorY = (short) HIWORD (lp);
        RECT rc;
        GetWindowRect (hwnd, &rc);

        // Bottom kHandleH px strip → interactive (drag / right-click)
        if (cursorY >= rc.bottom - VUMeterComponent::kHandleH)
            return HTCLIENT;

        return HTTRANSPARENT;   // rest of window: click-through
    }

    // Dispatch to original JUCE WndProc stored as a window property
    auto* original = (WNDPROC) GetProp (hwnd, "VUMeterOriginalProc");
    return original ? CallWindowProc (original, hwnd, msg, wp, lp)
                    : DefWindowProc  (hwnd, msg, wp, lp);
}

#endif   // JUCE_WINDOWS

// =========================================================================
// VUMeterWindow — public + private method implementations
// =========================================================================

void VUMeterWindow::show()
{
    if (! component_->isOnDesktop())
    {
        component_->addToDesktop (juce::ComponentPeer::windowIsTemporary);
        applyWin32Styles();
    }
    component_->setVisible (true);
    component_->toFront (false);
}

void VUMeterWindow::applyWin32Styles()
{
#if JUCE_WINDOWS
    auto* peer = component_->getPeer();
    if (peer == nullptr) return;

    auto* hwnd = (HWND) peer->getNativeHandle();
    nativeHwnd_ = hwnd;

    // Add layered (alpha), tool window (no taskbar entry), topmost
    LONG_PTR ex = GetWindowLongPtr (hwnd, GWL_EXSTYLE);
    ex |= WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    SetWindowLongPtr (hwnd, GWL_EXSTYLE, ex);

    // 90% opacity
    SetLayeredWindowAttributes (hwnd, 0, 230, LWA_ALPHA);

    // Force topmost placement
    SetWindowPos (hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // Subclass WndProc — store original proc as a window property
    LONG_PTR original = SetWindowLongPtr (hwnd, GWLP_WNDPROC,
                                           (LONG_PTR) vuMeterSubclassProc);
    SetProp (hwnd, "VUMeterOriginalProc", (HANDLE) original);
#endif
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
