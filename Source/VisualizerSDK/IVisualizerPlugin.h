#pragma once

#include "IAudioSource.h"

// =============================================================================
// IVisualizerPlugin  — LVH Visualizer SDK  (Phase A)
//
// Abstract base class for all LVH visualizer plugins.
// A DLL exporting createVisualizer() can be dropped into the
// <LVH.exe dir>/Visualizers/ folder and will be loaded at startup.
//
// Minimal compile dependencies:
//   - IAudioSource.h (this SDK)
//   - JUCE headers (juce_graphics, juce_opengl) for the render() signature
// =============================================================================

// Forward-declare JUCE types to keep dependency surface minimal when the
// plugin just needs to compile its own translation unit.
namespace juce
{
    class Graphics;
    class OpenGLContext;
}

class IVisualizerPlugin
{
public:
    virtual ~IVisualizerPlugin() = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /** Called once after the DLL is loaded.
        `source` is owned by the host and remains valid until shutdown(). */
    virtual void initialise (IAudioSource* source) = 0;

    /** Called on every repaint cycle by VisualizerManager.
        `g`              — JUCE 2-D graphics context (always valid).
        `openGLContext`  — pointer to the host's OpenGL context;
                           may be nullptr if OpenGL is not available. */
    virtual void render (juce::Graphics& g, juce::OpenGLContext* openGLContext) = 0;

    /** Called before the DLL is unloaded.  Release all resources here. */
    virtual void shutdown() = 0;
};

// =============================================================================
// DLL entry point
// Every visualizer DLL must export this symbol with C linkage:
//
//   extern "C" __declspec(dllexport)
//   IVisualizerPlugin* createVisualizer();
//
// The host calls createVisualizer() to obtain one plugin instance.
// The host is responsible for eventually calling shutdown() and delete.
// =============================================================================
using CreateVisualizerFunc = IVisualizerPlugin* (*)();
