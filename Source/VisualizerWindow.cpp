// VisualizerWindow.cpp — Win32 platform implementation (opacity)
// windows.h must be included BEFORE JUCE headers in this TU to avoid
// wingdi.h / juce::Rectangle name clash in other translation units.
#if defined(_WIN32) || defined(_WIN64)
 #include <windows.h>
#endif

#include "VisualizerWindow.h"

// =========================================================================
// VisualizerWindow — Win32 style application
// =========================================================================

void VisualizerWindow::applyWin32Styles()
{
#if JUCE_WINDOWS
    if (nativeHwnd_ != nullptr) return;   // idempotent

    auto* peer = getPeer();
    if (peer == nullptr) return;

    nativeHwnd_ = (HWND) peer->getNativeHandle();

    // Apply any opacity that was set before the handle was available.
    if (opacity_ < 1.0f)
        setOpacity (opacity_);
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
