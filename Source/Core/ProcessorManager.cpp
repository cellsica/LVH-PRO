#include "ProcessorManager.h"

// =============================================================================
// ProcessorManager
// =============================================================================

ProcessorManager::ProcessorManager() = default;

ProcessorManager::~ProcessorManager()
{
    unloadAll();
}

// ── Discovery ─────────────────────────────────────────────────────────────────

void ProcessorManager::scanOnly (const juce::File& processorsDir)
{
    unloadAll();
    discovered_.clear();

    if (! processorsDir.isDirectory())
    {
        DBG ("[ProcessorManager] Directory not found: " + processorsDir.getFullPathName());
        return;
    }

    DBG ("[ProcessorManager] Scanning (scan-only): " + processorsDir.getFullPathName());

    for (const auto& dllFile : processorsDir.findChildFiles (
             juce::File::findFiles, false, "*.dll"))
    {
        auto entry = std::make_unique<DiscoveredProcessor>();
        entry->dllFile  = dllFile;
        entry->stemName = dllFile.getFileNameWithoutExtension();
        discovered_.push_back (std::move (entry));
        DBG ("[ProcessorManager] Discovered: " + discovered_.back()->stemName);
    }

    DBG ("[ProcessorManager] " + juce::String ((int) discovered_.size())
         + " processor(s) discovered.");
}

// ── Start / Stop ──────────────────────────────────────────────────────────────

bool ProcessorManager::startProcessor (int index)
{
    if (index < 0 || index >= (int) discovered_.size())
        return false;

    auto& entry = discovered_[(size_t) index];

    if (entry->isRunning())
        return true;  // Already running — no-op.

    entry->library = std::make_unique<juce::DynamicLibrary>();
    if (! entry->library->open (entry->dllFile.getFullPathName()))
    {
        DBG ("[ProcessorManager] Failed to load DLL: " + entry->dllFile.getFileName());
        entry->library.reset();
        return false;
    }

    auto* createFn = reinterpret_cast<CreateProcessorFunc> (
        entry->library->getFunction ("createProcessor"));

    if (createFn == nullptr)
    {
        DBG ("[ProcessorManager] No createProcessor() export: " + entry->dllFile.getFileName());
        entry->library.reset();
        return false;
    }

    entry->instance = createFn();
    if (entry->instance == nullptr)
    {
        DBG ("[ProcessorManager] createProcessor() returned null: " + entry->dllFile.getFileName());
        entry->library.reset();
        return false;
    }

    entry->instance->initialise (lastSampleRate_, lastBufferSize_);
    DBG ("[ProcessorManager] Started: " + entry->stemName);
    return true;
}

void ProcessorManager::stopProcessor (int index)
{
    if (index < 0 || index >= (int) discovered_.size())
        return;

    auto& entry = discovered_[(size_t) index];

    if (! entry->isRunning())
        return;  // Not running — no-op.

    entry->instance->shutdown();
    delete entry->instance;
    entry->instance = nullptr;
    entry->library.reset();
    DBG ("[ProcessorManager] Stopped: " + entry->stemName);
}

void ProcessorManager::unloadAll()
{
    for (int i = 0; i < (int) discovered_.size(); ++i)
        stopProcessor (i);
}

// ── Device configuration ──────────────────────────────────────────────────────

void ProcessorManager::prepareAll (double sampleRate, int maxBufferSize)
{
    lastSampleRate_ = sampleRate;
    lastBufferSize_ = maxBufferSize;

    for (auto& entry : discovered_)
        if (entry->isRunning())
            entry->instance->initialise (sampleRate, maxBufferSize);
}

// ── Query — discovered list ───────────────────────────────────────────────────

juce::String ProcessorManager::getName (int index) const
{
    if (index < 0 || index >= (int) discovered_.size())
        return {};

    const auto& entry = discovered_[(size_t) index];
    if (entry->isRunning())
        return juce::String (entry->instance->getName());
    return entry->stemName;
}

bool ProcessorManager::isRunning (int index) const
{
    if (index < 0 || index >= (int) discovered_.size())
        return false;
    return discovered_[(size_t) index]->isRunning();
}

unsigned int ProcessorManager::getAccentColour (int index) const
{
    if (index < 0 || index >= (int) discovered_.size())
        return 0xff556688u;

    const auto& entry = discovered_[(size_t) index];
    if (entry->isRunning())
        return entry->instance->getAccentColour();
    return 0xff556688u;  // Default until started.
}

// ── Query — active instances ──────────────────────────────────────────────────

std::vector<IProcessorPlugin*> ProcessorManager::getActiveInstances() const
{
    std::vector<IProcessorPlugin*> result;
    for (const auto& entry : discovered_)
        if (entry->isRunning())
            result.push_back (entry->instance);
    return result;
}

// ── Legacy compat ─────────────────────────────────────────────────────────────

juce::StringArray ProcessorManager::getProcessorNames() const
{
    juce::StringArray names;
    for (int i = 0; i < (int) discovered_.size(); ++i)
        names.add (getName (i));
    return names;
}
