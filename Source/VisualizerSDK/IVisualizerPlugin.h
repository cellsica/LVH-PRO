#pragma once

/**
 * @file IVisualizerPlugin.h
 * @brief LVH Visualizer SDK — abstract base class for all visualizer plugins.
 */

#include "IAudioSource.h"

// Forward-declare JUCE types to keep dependency surface minimal when the
// plugin just needs to compile its own translation unit.
namespace juce
{
    class Graphics;
    class OpenGLContext;
}

/**
 * @class IVisualizerPlugin
 * @brief Abstract interface that every LVH visualizer DLL must implement.
 *
 * A DLL placed in the `<LVH.exe dir>/Visualizers/` folder is loaded at
 * startup by VisualizerManager.  The DLL must export a single C-linkage
 * factory function:
 *
 * @code
 * extern "C" __declspec(dllexport)
 * IVisualizerPlugin* createVisualizer();
 * @endcode
 *
 * **Lifecycle:**
 * 1. Host calls `createVisualizer()` to obtain an instance.
 * 2. Host calls `initialise()` once with an IAudioSource pointer.
 * 3. Host calls `render()` on every repaint cycle (message thread).
 * 4. Host calls `shutdown()` before unloading the DLL.
 * 5. Host calls `delete` on the instance.
 *
 * All methods are called from the message thread unless otherwise noted.
 * The plugin does **not** own the IAudioSource passed to initialise().
 */
class IVisualizerPlugin
{
public:
    virtual ~IVisualizerPlugin() = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Called once after the DLL is loaded.
     *
     * Plugins should store @p source and use it during render() to query
     * audio data.  The pointer remains valid until shutdown() is called.
     *
     * @param source  Non-owning pointer to the host's IAudioSource.
     *                Never nullptr when called by the host.
     */
    virtual void initialise (IAudioSource* source) = 0;

    /**
     * @brief Called on every repaint cycle to draw the visualizer frame.
     *
     * @param g              JUCE 2-D graphics context.  Always valid.
     *                       The clip region covers the full component bounds.
     * @param openGLContext  Pointer to the host's OpenGL context, or nullptr
     *                       if OpenGL is not available.  Plugins that use only
     *                       the JUCE 2-D API can ignore this parameter.
     */
    virtual void render (juce::Graphics& g, juce::OpenGLContext* openGLContext) = 0;

    /**
     * @brief Called before the DLL is unloaded.
     *
     * Release all resources here (GPU objects, threads, etc.).
     * After this call the host will invoke `delete` on the instance.
     */
    virtual void shutdown() = 0;

    // -------------------------------------------------------------------------
    // Preferred window size (optional override)
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the preferred client-area width for this visualizer.
     *
     * VisualizerWindow uses this value when the window is first created.
     * Override to request a size suited to your layout.
     *
     * @return Width in pixels.  Default: 500.
     */
    virtual int getPreferredWidth()  const { return 500; }

    /**
     * @brief Returns the preferred client-area height for this visualizer.
     *
     * @return Height in pixels.  Default: 500.
     */
    virtual int getPreferredHeight() const { return 500; }
};

// =============================================================================
// DLL entry point type alias
// =============================================================================

/** @brief Function pointer type for the DLL factory entry point. */
using CreateVisualizerFunc = IVisualizerPlugin* (*)();
