#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

using namespace juce;

// Background thread that scans for VST3 plugins using PluginDirectoryScanner.
// Progress and completion callbacks are always delivered on the message thread.
class PluginScanThread : public Thread
{
public:
    PluginScanThread (KnownPluginList& list,
                      const File& deadMansPedalFile,
                      StringArray extraPaths,
                      std::function<void(const String& filename)> onProgress,
                      std::function<void()> onComplete)
        : Thread ("LVH-PluginScan"),
          knownPlugins (list),
          deadMansPedal (deadMansPedalFile),
          extraScanPaths (std::move (extraPaths)),
          progressCallback (std::move (onProgress)),
          completeCallback (std::move (onComplete))
    {}

    ~PluginScanThread() override
    {
        signalThreadShouldExit();
        waitForThreadToExit (5000);
    }

    void run() override
    {
        VST3PluginFormat vst3;
        auto searchPaths = vst3.getDefaultLocationsToSearch();
        for (const auto& p : extraScanPaths)
            if (File (p).isDirectory())
                searchPaths.add (p, true);

        PluginDirectoryScanner scanner (knownPlugins, vst3, searchPaths,
                                        true, deadMansPedal, false);
        String currentPlugin;
        while (! threadShouldExit() && scanner.scanNextFile (true, currentPlugin))
        {
            String name = File (currentPlugin).getFileName();
            auto cb = progressCallback;
            MessageManager::callAsync ([cb, name] { cb (name); });
        }

        auto cb = completeCallback;
        MessageManager::callAsync ([cb] { cb(); });
    }

private:
    KnownPluginList& knownPlugins;
    File deadMansPedal;
    StringArray extraScanPaths;
    std::function<void(const String&)> progressCallback;
    std::function<void()> completeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanThread)
};
