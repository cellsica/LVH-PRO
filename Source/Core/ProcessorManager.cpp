#include "ProcessorManager.h"

// =============================================================================
// ProcessorManager
// =============================================================================

ProcessorManager::ProcessorManager() = default;

ProcessorManager::~ProcessorManager()
{
    unloadAll();
}

// ── Plugin management ─────────────────────────────────────────────────────────

void ProcessorManager::scanAndLoad (const juce::File& processorsDir)
{
    unloadAll();
    processorsDir_ = processorsDir;

    if (! processorsDir.isDirectory())
    {
        DBG ("[ProcessorManager] Directory not found: " + processorsDir.getFullPathName());
        return;
    }

    DBG ("[ProcessorManager] Scanning: " + processorsDir.getFullPathName());

    for (const auto& dllFile : processorsDir.findChildFiles (
             juce::File::findFiles, false, "*.dll"))
    {
        auto proc     = std::make_unique<LoadedProcessor>();
        proc->name    = dllFile.getFileNameWithoutExtension();
        proc->library = std::make_unique<juce::DynamicLibrary>();

        if (! proc->library->open (dllFile.getFullPathName()))
        {
            DBG ("[ProcessorManager] Failed to load DLL: " + dllFile.getFileName());
            continue;
        }

        auto* createFn = reinterpret_cast<CreateProcessorFunc> (
            proc->library->getFunction ("createProcessor"));

        if (createFn == nullptr)
        {
            DBG ("[ProcessorManager] No createProcessor() export in: "
                 + dllFile.getFileName());
            continue;
        }

        proc->instance = createFn();
        if (proc->instance == nullptr)
        {
            DBG ("[ProcessorManager] createProcessor() returned null: "
                 + dllFile.getFileName());
            continue;
        }

        proc->instance->initialise (lastSampleRate_, lastBufferSize_);

        DBG ("[ProcessorManager] Loaded: " + proc->name);
        processors_.push_back (std::move (proc));
    }

    DBG ("[ProcessorManager] " + juce::String ((int)processors_.size())
         + " processor(s) loaded.");
}

void ProcessorManager::unloadAll()
{
    processors_.clear();  // ~LoadedProcessor calls shutdown() + delete on each instance
}

void ProcessorManager::prepareAll (double sampleRate, int maxBufferSize)
{
    lastSampleRate_ = sampleRate;
    lastBufferSize_ = maxBufferSize;

    // Re-initialise loaded plugins so they can reallocate internal buffers.
    // ProcessorPluginNode::prepareToPlay() will also call initialise() when
    // the audio graph is rebuilt, but calling it here ensures plugins are
    // ready even before the next graph rebuild.
    for (auto& proc : processors_)
        if (proc->instance != nullptr)
            proc->instance->initialise (sampleRate, maxBufferSize);
}

// ── Query ─────────────────────────────────────────────────────────────────────

juce::StringArray ProcessorManager::getProcessorNames() const
{
    juce::StringArray names;
    for (auto& p : processors_)
        names.add (p->name);
    return names;
}

std::vector<IProcessorPlugin*> ProcessorManager::getPluginInstances() const
{
    std::vector<IProcessorPlugin*> instances;
    instances.reserve (processors_.size());
    for (auto& p : processors_)
        if (p->instance != nullptr)
            instances.push_back (p->instance);
    return instances;
}
