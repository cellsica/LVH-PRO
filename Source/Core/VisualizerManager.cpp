#include "VisualizerManager.h"
#include "AudioEngine.h"

// =============================================================================
// VisualizerManager
// =============================================================================

VisualizerManager::VisualizerManager() = default;

VisualizerManager::~VisualizerManager()
{
    stopTimer();
    unloadAll();
}

// -----------------------------------------------------------------------------
// Plugin management
// -----------------------------------------------------------------------------

void VisualizerManager::scanAndLoad (const juce::File& vizDirectory)
{
    unloadAll();
    currentPluginIndex_ = 0;
    vizDirectory_ = vizDirectory;   // remember for rescan()

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
        plugin->instance->setThemeColors (themeMain_, themeAccent_);  // apply current theme

        DBG ("[VisualizerManager] Loaded plugin: " + plugin->name);
        plugins_.push_back (std::move (plugin));
    }

    DBG ("[VisualizerManager] " + juce::String (plugins_.size()) + " plugin(s) loaded.");
}

void VisualizerManager::rescan()
{
    if (! vizDirectory_.isDirectory())
    {
        DBG ("[VisualizerManager] rescan(): no directory set — call scanAndLoad first.");
        return;
    }

    // Preserve the current selection across the reload
    int savedIndex = currentPluginIndex_;
    scanAndLoad (vizDirectory_);

    if (! plugins_.empty())
        currentPluginIndex_ = juce::jlimit (0, (int)plugins_.size() - 1, savedIndex);

    DBG ("[VisualizerManager] Rescan complete. " + juce::String (plugins_.size()) + " plugin(s).");
}

void VisualizerManager::unloadAll()
{
    // Plugins are shutdown() + deleted in LoadedPlugin destructor,
    // then the DynamicLibrary is closed.
    plugins_.clear();
}

juce::StringArray VisualizerManager::getPluginNames() const
{
    juce::StringArray names;
    for (auto& p : plugins_)
        names.add (p->name);
    return names;
}

// -----------------------------------------------------------------------------
// Plugin selection
// -----------------------------------------------------------------------------

void VisualizerManager::setCurrentPlugin (int index)
{
    if (plugins_.empty()) return;
    currentPluginIndex_ = juce::jlimit (0, (int)plugins_.size() - 1, index);
    DBG ("[VisualizerManager] Switched to plugin " + juce::String (currentPluginIndex_)
         + ": " + plugins_[currentPluginIndex_]->name);
    if (onStateChanged) onStateChanged();
}

void VisualizerManager::nextPlugin()
{
    if (plugins_.size() <= 1) return;

    if (switchMode_ == SwitchMode::Random)
    {
        int next;
        do { next = random_.nextInt ((int)plugins_.size()); }
        while (next == currentPluginIndex_);
        currentPluginIndex_ = next;
    }
    else
    {
        currentPluginIndex_ = (currentPluginIndex_ + 1) % (int)plugins_.size();
    }

    DBG ("[VisualizerManager] Auto-switched to plugin " + juce::String (currentPluginIndex_)
         + ": " + plugins_[currentPluginIndex_]->name);
}

// -----------------------------------------------------------------------------
// Auto-switching
// -----------------------------------------------------------------------------

void VisualizerManager::setSwitchMode (SwitchMode mode)
{
    switchMode_ = mode;

    if (mode == SwitchMode::Manual)
    {
        stopTimer();
        DBG ("[VisualizerManager] Auto-switch: OFF");
    }
    else
    {
        startTimer (switchIntervalSec_ * 1000);
        DBG ("[VisualizerManager] Auto-switch: "
             + juce::String (mode == SwitchMode::Random ? "Random" : "Sequential")
             + " every " + juce::String (switchIntervalSec_) + "s");
    }

    if (onStateChanged) onStateChanged();
}

void VisualizerManager::setSwitchInterval (int seconds)
{
    switchIntervalSec_ = seconds;
    if (switchMode_ != SwitchMode::Manual)
        startTimer (seconds * 1000);
    if (onStateChanged) onStateChanged();
}

void VisualizerManager::timerCallback()
{
    nextPlugin();
}

// -----------------------------------------------------------------------------
// Theme color sync (Phase E)
// -----------------------------------------------------------------------------

void VisualizerManager::notifyThemeColors (uint32_t mainColor, uint32_t accentColor)
{
    themeMain_   = mainColor;
    themeAccent_ = accentColor;
    for (auto& p : plugins_)
        if (p->instance != nullptr)
            p->instance->setThemeColors (mainColor, accentColor);
}

// -----------------------------------------------------------------------------
// Rendering — Phase D: render current plugin only
// -----------------------------------------------------------------------------

void VisualizerManager::render (juce::Graphics& g, juce::OpenGLContext* openGLContext)
{
    if (plugins_.empty()) return;

    int idx = juce::jlimit (0, (int)plugins_.size() - 1, currentPluginIndex_);
    if (plugins_[idx]->instance != nullptr)
        plugins_[idx]->instance->render (g, openGLContext);
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
