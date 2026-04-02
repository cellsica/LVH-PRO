#pragma once

/**
 * @file ProcessorManager.h
 * @brief Lifecycle manager for IProcessorPlugin DLL plugins loaded from the
 *        `<exe dir>/Processors/` directory.
 */

#include <JuceHeader.h>
#include "../ProcessorSDK/IProcessorPlugin.h"
#include <vector>
#include <memory>

/**
 * @class ProcessorManager
 * @brief Manages the lifecycle of IProcessorPlugin DLL plugins.
 *
 * Responsibilities:
 *  1. Scan `<LVH.exe dir>/Processors/` for *.dll files.
 *  2. Load each DLL via juce::DynamicLibrary, resolve `createProcessor()`.
 *  3. Call IProcessorPlugin::initialise() on load and on device changes.
 *  4. Call IProcessorPlugin::shutdown() + delete on unload.
 *  5. Expose raw plugin pointers for AudioEngine to wrap in ProcessorPluginNode.
 *
 * **Ownership:**
 * - ProcessorManager owns the DynamicLibrary and IProcessorPlugin instances.
 * - AudioEngine wraps raw IProcessorPlugin* in ProcessorPluginNode (non-owning).
 *
 * **Thread safety:**
 * All public methods must be called from the message thread.
 */
class ProcessorManager
{
public:
    /** @brief Default constructor.  No plugins are loaded until scanAndLoad() is called. */
    ProcessorManager();

    /** @brief Calls unloadAll() to ensure every plugin receives shutdown() before destruction. */
    ~ProcessorManager();

    // ── Plugin management ─────────────────────────────────────────────────────

    /**
     * @brief Scan @p processorsDir for *.dll files and load each one.
     *
     * Any previously loaded plugins are unloaded first.
     * Each successfully loaded plugin receives an initialise() call with the
     * last-known sample rate and buffer size.
     *
     * @param processorsDir  Directory to scan (e.g. `<exe dir>/Processors/`).
     */
    void scanAndLoad (const juce::File& processorsDir);

    /** @brief Shutdown and unload all currently loaded plugins. */
    void unloadAll();

    /**
     * @brief Notify all loaded plugins of a device configuration change.
     *
     * Stores the new values and calls initialise() on every loaded plugin.
     * Called by UIManager when AudioDeviceManager reports a change.
     *
     * @param sampleRate    New sample rate in Hz.
     * @param maxBufferSize New maximum buffer size in samples.
     */
    void prepareAll (double sampleRate, int maxBufferSize);

    // ── Query ─────────────────────────────────────────────────────────────────

    /** @brief Number of successfully loaded plugins. */
    int getNumProcessors() const noexcept { return (int)processors_.size(); }

    /** @brief Returns the display name of each loaded plugin. */
    juce::StringArray getProcessorNames() const;

    /**
     * @brief Returns raw pointers to all loaded IProcessorPlugin instances.
     *
     * Pointers are valid until the next call to unloadAll() or scanAndLoad().
     * AudioEngine stores a copy of this vector via setProcessorPlugins().
     */
    std::vector<IProcessorPlugin*> getPluginInstances() const;

private:
    /**
     * @struct LoadedProcessor
     * @brief Owns one successfully loaded DLL and its IProcessorPlugin instance.
     *
     * Destruction order is critical: the instance must be shut down and deleted
     * **before** the DynamicLibrary handle is released, so that the DLL's code
     * remains mapped while the destructor runs.  The member declaration order
     * (library first, instance second, but deleted in reverse) combined with the
     * explicit destructor guarantees this.
     */
    struct LoadedProcessor
    {
        std::unique_ptr<juce::DynamicLibrary> library;   ///< Keeps the DLL mapped.
        IProcessorPlugin* instance = nullptr;             ///< Plugin object allocated inside the DLL.
        juce::String      name;                           ///< DLL filename without extension.

        ~LoadedProcessor()
        {
            if (instance != nullptr)
            {
                instance->shutdown();
                delete instance;
                instance = nullptr;
            }
            // DynamicLibrary is closed by unique_ptr after instance is deleted.
        }
    };

    std::vector<std::unique_ptr<LoadedProcessor>> processors_; ///< All successfully loaded plugins.
    juce::File processorsDir_;       ///< Directory last passed to scanAndLoad(); used by rescan.
    double lastSampleRate_ = 44100.0; ///< Cached sample rate applied on load and prepareAll().
    int    lastBufferSize_ = 512;     ///< Cached buffer size applied on load and prepareAll().

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorManager)
};
