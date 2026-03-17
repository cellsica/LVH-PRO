#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

using namespace juce;

// Represents a single plugin slot.
// The AudioProcessorGraph owns the actual AudioPluginInstance;
// PluginSlot holds a raw reference, the node ID, the editor, and preset state.
class PluginSlot
{
public:
    PluginSlot() = default;

    // Called after the plugin node has been added to the graph.
    void attach (AudioPluginInstance* rawPtr, AudioProcessorGraph::NodeID id)
    {
        pluginRawPtr = rawPtr;
        nodeID = id;
    }

    // Called before removing the node from the graph.
    void detach()
    {
        releaseEditor();
        pluginRawPtr = nullptr;
        nodeID = {};
        currentPresetName = {};
        currentPresetFile = {};
    }

    bool isLoaded() const { return pluginRawPtr != nullptr; }
    AudioPluginInstance* getProcessor() const { return pluginRawPtr; }
    AudioProcessorGraph::NodeID getNodeID() const { return nodeID; }

    // Snapshot current plugin state into presetState.
    void saveState()
    {
        if (pluginRawPtr != nullptr)
            pluginRawPtr->getStateInformation (presetState);
    }

    // Restore previously saved state into plugin.
    void restoreState()
    {
        if (pluginRawPtr != nullptr && presetState.getSize() > 0)
            pluginRawPtr->setStateInformation (presetState.getData(), (int) presetState.getSize());
    }

    // Returns (and caches) the plugin editor. Creates it on first call.
    AudioProcessorEditor* getOrCreateEditor()
    {
        if (pluginRawPtr == nullptr) return nullptr;
        if (editor == nullptr)
            editor.reset (pluginRawPtr->createEditorIfNeeded());
        return editor.get();
    }

    void releaseEditor() { editor.reset(); }

    // Returns the plugin's reported processing latency in samples (for ADC).
    int getLatencyInSamples() const
    {
        return pluginRawPtr != nullptr ? pluginRawPtr->getLatencySamples() : 0;
    }

    void bypass (bool shouldBypass) { bypassed = shouldBypass; }
    bool isBypassed() const { return bypassed; }

    // =========================================================================
    // Preset management
    // =========================================================================

    const String& getCurrentPresetName() const { return currentPresetName; }
    const File&   getCurrentPresetFile() const { return currentPresetFile; }

    // Sanitize a display name so it can be used as a filename.
    static String sanitizeForFilename (const String& name)
    {
        return name.trim()
                   .replaceCharacter ('\\', '_')
                   .replaceCharacter ('/',  '_')
                   .replaceCharacter (':',  '_')
                   .replaceCharacter ('*',  '_')
                   .replaceCharacter ('?',  '_')
                   .replaceCharacter ('"',  '_')
                   .replaceCharacter ('<',  '_')
                   .replaceCharacter ('>',  '_')
                   .replaceCharacter ('|',  '_');
    }

    // Returns the folder where presets for the given plugin are stored.
    static File getPresetsFolder (const String& pluginName)
    {
        return File::getSpecialLocation (File::userApplicationDataDirectory)
                   .getChildFile ("cellsica/LIGHT-VST-HOST/Presets")
                   .getChildFile (sanitizeForFilename (pluginName));
    }

    // Save current plugin state as a named preset XML file.
    // Returns true on success.
    bool savePreset (const String& presetName)
    {
        if (pluginRawPtr == nullptr || presetName.isEmpty())
            return false;

        MemoryBlock state;
        pluginRawPtr->getStateInformation (state);

        auto xml = std::make_unique<XmlElement> ("Preset");
        xml->setAttribute ("PresetName",  presetName);
        xml->setAttribute ("PluginName",  pluginRawPtr->getName());
        xml->setAttribute ("StateData",   state.toBase64Encoding());

        auto folder = getPresetsFolder (pluginRawPtr->getName());
        folder.createDirectory();

        auto file = folder.getChildFile (sanitizeForFilename (presetName) + ".xml");
        if (xml->writeTo (file))
        {
            currentPresetName = presetName;
            currentPresetFile = file;
            return true;
        }
        return false;
    }

    // Load plugin state from a preset XML file.
    // Returns true on success.
    bool loadPreset (const File& file)
    {
        if (pluginRawPtr == nullptr || ! file.existsAsFile())
            return false;

        auto xml = XmlDocument::parse (file);
        if (xml == nullptr)
            return false;

        MemoryBlock state;
        if (! state.fromBase64Encoding (xml->getStringAttribute ("StateData")))
            return false;

        if (state.getSize() == 0)
            return false;

        pluginRawPtr->setStateInformation (state.getData(), (int) state.getSize());
        currentPresetName = xml->getStringAttribute ("PresetName",
                                                     file.getFileNameWithoutExtension());
        currentPresetFile = file;
        return true;
    }

private:
    AudioPluginInstance* pluginRawPtr = nullptr;
    std::unique_ptr<AudioProcessorEditor> editor;
    AudioProcessorGraph::NodeID nodeID {};
    MemoryBlock presetState;
    bool bypassed = false;

    String currentPresetName;
    File   currentPresetFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSlot)
};
