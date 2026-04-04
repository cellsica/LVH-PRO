#pragma once

/**
 * @file ProcessorManager.h
 * @brief Lifecycle manager for IProcessorPlugin DLL plugins loaded from the
 *        `<exe dir>/Processors/` directory.
 *
 * @note Phase A (Mission 053): Scan and Start/Stop are now separated.
 *       scanOnly() discovers DLLs without creating instances.
 *       startProcessor() / stopProcessor() manage individual plugin lifecycle.
 */

#include <JuceHeader.h>
#include "../ProcessorSDK/IProcessorPlugin.h"
#include <vector>
#include <memory>

/**
 * @class ProcessorManager
 * @brief Manages the discovery and lifecycle of IProcessorPlugin DLL plugins.
 *
 * **Scan vs. Start separation (Mission 053):**
 *  - scanOnly() scans `Processors/` and records DLL paths — no instances are created.
 *  - startProcessor(index) loads the DLL, creates an instance, and calls initialise().
 *  - stopProcessor(index) calls shutdown(), deletes the instance, and unloads the DLL.
 *
 * **Ownership:**
 * - ProcessorManager owns DynamicLibrary handles and IProcessorPlugin instances.
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

    // ── Discovery ─────────────────────────────────────────────────────────────

    /**
     * @brief Scan @p processorsDir for *.dll files and record them.
     *
     * Previously running plugins are stopped first.
     * No DLL is loaded or instantiated — only file paths are recorded.
     *
     * @param processorsDir  Directory to scan (e.g. `<exe dir>/Processors/`).
     */
    void scanOnly (const juce::File& processorsDir);

    // ── Start / Stop ──────────────────────────────────────────────────────────

    /**
     * @brief Load the DLL at @p index and create a running plugin instance.
     *
     * If the plugin at @p index is already running, this is a no-op and returns true.
     *
     * @param index  Zero-based index into the discovered list.
     * @return true on success, false if loading or instantiation failed.
     */
    bool startProcessor (int index);

    /**
     * @brief Stop and unload the plugin instance at @p index.
     *
     * Calls shutdown() and delete on the instance, then closes the DLL handle.
     * If the plugin is not running, this is a no-op.
     *
     * @param index  Zero-based index into the discovered list.
     */
    void stopProcessor (int index);

    /** @brief Stop and unload all currently running plugins. */
    void unloadAll();

    // ── Device configuration ──────────────────────────────────────────────────

    /**
     * @brief Notify all running plugins of an audio device configuration change.
     *
     * Caches the values and calls initialise() on every running plugin instance.
     *
     * @param sampleRate    New sample rate in Hz.
     * @param maxBufferSize New maximum buffer size in samples.
     */
    void prepareAll (double sampleRate, int maxBufferSize);

    // ── Query — discovered list ───────────────────────────────────────────────

    /** @brief Number of DLL files found by the last scanOnly() call. */
    int getNumDiscovered() const noexcept { return (int) discovered_.size(); }

    /**
     * @brief Returns the display name of the discovered plugin at @p index.
     *
     * When the plugin is running, this is the value returned by getName().
     * Otherwise it is the DLL filename without extension.
     *
     * @param index  Zero-based index into the discovered list.
     */
    juce::String getName (int index) const;

    /**
     * @brief Returns whether the plugin at @p index is currently running.
     * @param index  Zero-based index into the discovered list.
     */
    bool isRunning (int index) const;

    /**
     * @brief Returns true if the running plugin at @p index has signalled that
     *        its own UI has been closed (IProcessorPlugin::hasUserRequestedClose).
     *
     * Always returns false when the plugin is not running.
     *
     * @param index  Zero-based index into the discovered list.
     */
    bool hasUserRequestedClose (int index) const;

    /**
     * @brief Returns the accent colour of the plugin at @p index.
     *
     * Returns the value from IProcessorPlugin::getAccentColour() when running,
     * or the default colour (0xff556688) when not yet started.
     *
     * @param index  Zero-based index into the discovered list.
     */
    unsigned int getAccentColour (int index) const;

    // ── Query — active instances ──────────────────────────────────────────────

    /**
     * @brief Returns raw pointers to all currently running plugin instances.
     *
     * The order matches the discovered list (skipping non-running entries).
     * AudioEngine stores a copy of this vector via setProcessorPlugins().
     * Pointers are valid until the next stop/unload call.
     */
    std::vector<IProcessorPlugin*> getActiveInstances() const;

    // ── Legacy compat (kept for callers that haven't migrated yet) ────────────

    /** @deprecated Use getActiveInstances(). Alias kept for minimal diff. */
    std::vector<IProcessorPlugin*> getPluginInstances() const { return getActiveInstances(); }

    /** @deprecated Use getNumDiscovered(). */
    int getNumProcessors() const noexcept { return getNumDiscovered(); }

    /** @deprecated Use getName(). */
    juce::StringArray getProcessorNames() const;

private:
    /**
     * @struct DiscoveredProcessor
     * @brief Represents one found DLL.  May or may not have a running instance.
     */
    struct DiscoveredProcessor
    {
        juce::File   dllFile;             ///< Full path to the DLL.
        juce::String stemName;            ///< DLL filename without extension.

        // ── Running state (null when not started) ──────────────────────────
        std::unique_ptr<juce::DynamicLibrary> library;   ///< Keeps the DLL mapped.
        IProcessorPlugin* instance = nullptr;             ///< Plugin object; null when stopped.

        ~DiscoveredProcessor()
        {
            if (instance != nullptr)
            {
                instance->shutdown();
                delete instance;
                instance = nullptr;
            }
            // DynamicLibrary closed by unique_ptr after instance is deleted.
        }

        bool isRunning() const noexcept { return instance != nullptr; }
    };

    std::vector<std::unique_ptr<DiscoveredProcessor>> discovered_; ///< All found DLLs.
    double lastSampleRate_ = 44100.0;
    int    lastBufferSize_ = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorManager)
};
