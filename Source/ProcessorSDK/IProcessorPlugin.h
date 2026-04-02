#pragma once

/**
 * @file IProcessorPlugin.h
 * @brief LVH Processor SDK — abstract base class for all processor plugins.
 *
 * @version 1.0
 */

/**
 * @class IProcessorPlugin
 * @brief Abstract interface that every LVH processor DLL must implement.
 *
 * A DLL placed in the `<LVH.exe dir>/Processors/` folder is loaded by
 * ProcessorManager.  The DLL must export a single C-linkage factory function:
 *
 * @code
 * extern "C" __declspec(dllexport)
 * IProcessorPlugin* createProcessor();
 * @endcode
 *
 * **Lifecycle:**
 * 1. Host calls `createProcessor()` to obtain an instance.
 * 2. Host calls `initialise()` once before the first `processBlock()`.
 * 3. Host calls `processBlock()` on every audio callback (audio thread).
 * 4. Host calls `shutdown()` before unloading the DLL.
 * 5. Host calls `delete` on the instance.
 *
 * **Thread safety:**
 * - `initialise()` and `shutdown()` are called from the message thread.
 * - `processBlock()` is called from the audio thread.
 * - `getName()` and `getAccentColour()` may be called from either thread;
 *   implementations must be safe to call from any thread (e.g. return a
 *   compile-time constant or a value set only during construction).
 *
 * **Audio format:**
 * - Stereo (2 channels) is the standard format.  `numChannels` will be 2
 *   unless the host configuration differs.
 * - Buffer layout: `buffers[channel][sample]`, non-interleaved.
 * - Input and output buffer pointers are always distinct (no in-place
 *   processing assumed).  Plugins may read `input` and write `output`
 *   independently.
 */
class IProcessorPlugin
{
public:
    virtual ~IProcessorPlugin() = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Called once before the first processBlock().
     *
     * Allocate buffers, reset state, and prepare internal resources here.
     * This method is called from the message thread.
     *
     * @param sampleRate     Host sample rate in Hz (e.g. 44100.0, 48000.0).
     * @param maxBufferSize  Maximum number of samples per processBlock() call.
     */
    virtual void initialise (double sampleRate, int maxBufferSize) = 0;

    /**
     * @brief Called before the DLL is unloaded.
     *
     * Release all resources (threads, file handles, allocated memory, etc.).
     * After this call the host will invoke `delete` on the instance.
     * This method is called from the message thread.
     */
    virtual void shutdown() = 0;

    // -------------------------------------------------------------------------
    // Audio processing
    // -------------------------------------------------------------------------

    /**
     * @brief Process one block of audio.
     *
     * Called from the audio thread on every audio callback.  Must return
     * within the callback deadline — avoid blocking operations (file I/O,
     * heap allocation, mutex locks) inside this method.
     *
     * @param input       Read-only input buffers: `input[channel][sample]`.
     *                    Contains `numChannels` pointers, each pointing to
     *                    `numSamples` floats.  Never nullptr.
     * @param output      Output buffers to fill: `output[channel][sample]`.
     *                    Contains `numChannels` pointers, each pointing to
     *                    `numSamples` floats.  Never nullptr.
     *                    Distinct from `input` — write here, read from `input`.
     * @param numChannels Number of audio channels (typically 2 for stereo).
     * @param numSamples  Number of samples in this block (≤ maxBufferSize
     *                    passed to initialise()).
     */
    virtual void processBlock (const float* const* input,
                               float**             output,
                               int                 numChannels,
                               int                 numSamples) = 0;

    // -------------------------------------------------------------------------
    // Identity
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the display name shown in the LVH Mixer Console strip.
     *
     * Must return a non-null, null-terminated C string.  The string must
     * remain valid for the lifetime of the plugin instance.  A compile-time
     * string literal is sufficient.
     *
     * @return Plugin display name (e.g. "My Looper").
     */
    virtual const char* getName() const = 0;

    // -------------------------------------------------------------------------
    // Optional overrides
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the preferred accent colour for the Mixer Console strip.
     *
     * The colour is encoded as a 32-bit ARGB value (0xAARRGGBB).
     * Override to give your plugin a distinctive colour in the mixer.
     *
     * @return ARGB colour value.  Default: 0xff556688 (muted blue).
     */
    virtual unsigned int getAccentColour() const { return 0xff556688; }
};

// =============================================================================
// DLL entry point
// =============================================================================

/**
 * @brief Function pointer type for the DLL factory entry point.
 *
 * Each processor DLL must export exactly one function with this signature:
 * @code
 * extern "C" __declspec(dllexport)
 * IProcessorPlugin* createProcessor();
 * @endcode
 */
using CreateProcessorFunc = IProcessorPlugin* (*)();
