#include "VisualizerManager.h"
#include "AudioEngine.h"

// =============================================================================
// VisualizerManager
// =============================================================================

VisualizerManager::VisualizerManager() = default;

VisualizerManager::~VisualizerManager()
{
    unloadAll();
}

// -----------------------------------------------------------------------------
// Plugin management
// -----------------------------------------------------------------------------

void VisualizerManager::scanAndLoad (const juce::File& vizDirectory)
{
    unloadAll();

    if (! vizDirectory.isDirectory())
    {
        DBG ("[VisualizerManager] Directory not found: " + vizDirectory.getFullPathName());
        return;
    }

    DBG ("[VisualizerManager] Scanning: " + vizDirectory.getFullPathName());

    for (const auto& dllFile : vizDirectory.findChildFiles (juce::File::findFiles, false, "*.dll"))
    {
        auto plugin = std::make_unique<LoadedPlugin>();
        plugin->name    = dllFile.getFileNameWithoutExtension();
        plugin->library = std::make_unique<juce::DynamicLibrary>();

        if (! plugin->library->open (dllFile.getFullPathName()))
        {
            DBG ("[VisualizerManager] Failed to load DLL: " + dllFile.getFileName());
            continue;
        }

        auto* createFn = reinterpret_cast<CreateVisualizerFunc> (
            plugin->library->getFunction ("createVisualizer"));

        if (createFn == nullptr)
        {
            DBG ("[VisualizerManager] No createVisualizer() in: " + dllFile.getFileName());
            continue;
        }

        plugin->instance = createFn();
        if (plugin->instance == nullptr)
        {
            DBG ("[VisualizerManager] createVisualizer() returned null: " + dllFile.getFileName());
            continue;
        }

        plugin->instance->initialise (this);

        DBG ("[VisualizerManager] Loaded plugin: " + plugin->name);
        plugins_.push_back (std::move (plugin));
    }

    DBG ("[VisualizerManager] " + juce::String (plugins_.size()) + " plugin(s) loaded.");
}

void VisualizerManager::unloadAll()
{
    // Plugins are shutdown() + deleted in LoadedPlugin destructor,
    // then the DynamicLibrary is closed.
    plugins_.clear();
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

void VisualizerManager::render (juce::Graphics& g, juce::OpenGLContext* openGLContext)
{
    for (auto& p : plugins_)
    {
        if (p->instance != nullptr)
            p->instance->render (g, openGLContext);
    }
}

// -----------------------------------------------------------------------------
// IAudioSource implementation
// -----------------------------------------------------------------------------

void VisualizerManager::getFFTData (float* buffer, int size)
{
    if (audioEngine_ != nullptr)
        audioEngine_->readFFTData (buffer, size);
    else
        std::fill_n (buffer, size, 0.f);
}

void VisualizerManager::getWaveformData (float* buffer, int size)
{
    if (audioEngine_ != nullptr)
        audioEngine_->readWaveformData (buffer, size);
    else
        std::fill_n (buffer, size, 0.f);
}
