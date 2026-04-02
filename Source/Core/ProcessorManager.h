#pragma once

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
    ProcessorManager();
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
    struct LoadedProcessor
    {
        std::unique_ptr<juce::DynamicLibrary> library;
        IProcessorPlugin* instance = nullptr;
        juce::String      name;

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

    std::vector<std::unique_ptr<LoadedProcessor>> processors_;
    juce::File processorsDir_;
    double lastSampleRate_ = 44100.0;
    int    lastBufferSize_ = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorManager)
};
