#include "ProjectSerializer.h"

ProjectSerializer::ProjectSerializer (const juce::OwnedArray<BridgeInstance>& bridges,
                                       AudioEngine&                             audioEngine,
                                       MidiRoutingManager&                      midiRouter,
                                       juce::AudioDeviceManager&                deviceManager,
                                       juce::ApplicationProperties&             appProperties)
    : bridges_      (bridges),
      audioEngine_  (audioEngine),
      midiRouter_   (midiRouter),
      deviceManager_(deviceManager),
      appProperties_(appProperties)
{}

// ── Closure-injection helpers ──────────────────────────────────────────────

juce::MemoryBlock ProjectSerializer::takePendingState (const juce::String& pluginPath)
{
    auto it = pendingPluginStates_.find (pluginPath);
    if (it == pendingPluginStates_.end()) return {};
    auto val = std::move (it->second);
    pendingPluginStates_.erase (it);
    return val;
}

std::optional<ProjectSerializer::MixerSettings>
ProjectSerializer::takePendingMixer (const juce::String& pluginPath)
{
    auto it = pendingMixerSettings_.find (pluginPath);
    if (it == pendingMixerSettings_.end()) return std::nullopt;
    auto val = std::move (it->second);
    pendingMixerSettings_.erase (it);
    return val;
}

juce::Rectangle<int> ProjectSerializer::takePendingBounds (const juce::String& pluginPath)
{
    auto it = pendingWindowBounds_.find (pluginPath);
    if (it == pendingWindowBounds_.end()) return {};
    auto val = it->second;
    pendingWindowBounds_.erase (it);
    return val;
}

// ── Save ──────────────────────────────────────────────────────────────────

void ProjectSerializer::saveProject (const juce::File& file)
{
    juce::Array<BridgeInstance*> connected;
    for (auto* b : bridges_)
        if (b->getState() == BridgeInstance::State::Connected)
            connected.add (b);

    if (connected.isEmpty())
    {
        writeProjectXml (file, {});
        return;
    }

    struct SaveContext
    {
        std::vector<BridgeStateEntry> entries;
        juce::File                    targetFile;
        int                           remaining = 0;
        bool                          written   = false;
    };
    auto ctx = std::make_shared<SaveContext>();
    ctx->targetFile = file;
    ctx->remaining  = connected.size();
    for (auto* b : connected)
        ctx->entries.push_back ({ b, {}, false });

    // Timeout: write whatever we have after 2 seconds
    juce::Timer::callAfterDelay (2000, [this, ctx] {
        if (! ctx->written)
        {
            ctx->written = true;
            writeProjectXml (ctx->targetFile, ctx->entries);
        }
    });

    // Wire onStateReceived for each connected bridge
    for (int i = 0; i < (int) ctx->entries.size(); ++i)
    {
        auto* b = ctx->entries[i].bridge;
        b->onStateReceived = [this, ctx, i] (BridgeInstance*, const juce::MemoryBlock& state) {
            if (ctx->written) return;
            ctx->entries[i].state    = state;
            ctx->entries[i].received = true;
            if (--ctx->remaining <= 0)
            {
                ctx->written = true;
                writeProjectXml (ctx->targetFile, ctx->entries);
            }
        };
        b->sendRequestState();
    }

    if (onMessage) onMessage ("Saving project (collecting plugin states)...");
}

void ProjectSerializer::writeProjectXml (const juce::File& file,
                                          const std::vector<BridgeStateEntry>& stateEntries)
{
    auto xml = std::make_unique<juce::XmlElement> ("LVH-Project");
    xml->setAttribute ("version", 1);

    auto* bridgesEl = xml->createNewChildElement ("Bridges");
    for (auto* b : bridges_)
    {
        if (b->getState() == BridgeInstance::State::Connected)
        {
            auto* el = bridgesEl->createNewChildElement ("Bridge");
            el->setAttribute ("plugin", b->getPluginPath());
            el->setAttribute ("role", b->getRole() == BridgeInstance::Role::Effect
                                      ? "effect" : "instrument");
            auto bounds = b->getWindowBounds();
            el->setAttribute ("x", bounds.getX());
            el->setAttribute ("y", bounds.getY());
            el->setAttribute ("w", bounds.getWidth());
            el->setAttribute ("h", bounds.getHeight());

            el->setAttribute ("gain",     (double) b->mixerGain.load());
            el->setAttribute ("pan",      (double) b->mixerPan.load());
            el->setAttribute ("muted",    b->mixerMuted.load()    ? 1 : 0);
            el->setAttribute ("bypassed", b->mixerBypassed.load() ? 1 : 0);
            if (b->mixerCustomName.isNotEmpty())
                el->setAttribute ("customName", b->mixerCustomName);
            if (b->mixerCustomColor.getAlpha() > 0)
                el->setAttribute ("customColor", b->mixerCustomColor.toDisplayString (true));

            for (const auto& entry : stateEntries)
                if (entry.bridge == b && entry.received && entry.state.getSize() > 0)
                {
                    el->setAttribute ("state", juce::Base64::toBase64 (
                        entry.state.getData(), entry.state.getSize()));
                    break;
                }
        }
    }

    auto* routeEl = xml->createNewChildElement ("MidiRouting");
    routeEl->setAttribute ("routeToAll", midiRouter_.isRouteToAll() ? 1 : 0);
    if (! midiRouter_.isRouteToAll())
        if (auto* t = midiRouter_.getTarget())
            routeEl->setAttribute ("targetPlugin", t->getPluginPath());

    auto* settingsEl = xml->createNewChildElement ("Settings");
    settingsEl->setAttribute ("octaveOffset", midiRouter_.getOctaveOffset());

    double vol = getMasterVolume ? getMasterVolume() : 1.0;
    settingsEl->setAttribute ("masterVolume", vol);

    if (auto* prefs = appProperties_.getUserSettings())
    {
        settingsEl->setAttribute ("transpose",     prefs->getIntValue ("transpose",     0));
        settingsEl->setAttribute ("channelFilter", prefs->getIntValue ("channelFilter", 0));
    }

    if (getCoreWindowBounds)
    {
        auto b = getCoreWindowBounds();
        settingsEl->setAttribute ("coreWindowX", b.getX());
        settingsEl->setAttribute ("coreWindowY", b.getY());
        settingsEl->setAttribute ("coreWindowW", b.getWidth());
        settingsEl->setAttribute ("coreWindowH", b.getHeight());
    }

    for (auto& d : juce::MidiInput::getAvailableDevices())
    {
        if (deviceManager_.isMidiInputDeviceEnabled (d.identifier))
        {
            settingsEl->setAttribute ("midiInputIdentifier", d.identifier);
            settingsEl->setAttribute ("midiInputName",       d.name);
            break;
        }
    }

    bool mixerVis = getMixerVisible ? getMixerVisible() : false;
    settingsEl->setAttribute ("mixerVisible", mixerVis ? 1 : 0);
    if (getMixerWindowBounds)
    {
        auto b = getMixerWindowBounds();
        settingsEl->setAttribute ("mixerWindowX", b.getX());
        settingsEl->setAttribute ("mixerWindowY", b.getY());
        settingsEl->setAttribute ("mixerWindowW", b.getWidth());
        settingsEl->setAttribute ("mixerWindowH", b.getHeight());
    }

    xml->writeTo (file);

    if (onMessage)
    {
        int savedStateCount = 0;
        for (const auto& entry : stateEntries)
            if (entry.received && entry.state.getSize() > 0) ++savedStateCount;

        onMessage ("Project saved: " + file.getFileNameWithoutExtension()
            + " (" + juce::String (savedStateCount) + "/"
            + juce::String ((int) stateEntries.size()) + " plugin states captured)");
    }

    for (auto& entry : stateEntries)
        if (entry.bridge != nullptr)
            entry.bridge->onStateReceived = nullptr;
}

// ── Load ──────────────────────────────────────────────────────────────────

void ProjectSerializer::loadProject (const juce::File& file)
{
    if (! file.existsAsFile()) return;

    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || xml->getTagName() != "LVH-Project")
    {
        if (onMessage) onMessage ("Failed to load project: " + file.getFileName());
        return;
    }

    if (onProjectResetRequired) onProjectResetRequired();

    pendingWindowBounds_.clear();
    pendingPluginStates_.clear();
    pendingMixerSettings_.clear();
    midiRouter_.resetForProjectLoad();

    if (auto* settingsEl = xml->getChildByName ("Settings"))
    {
        int targetOctave = settingsEl->getIntAttribute ("octaveOffset", 0);
        midiRouter_.applyOctaveShift (targetOctave - midiRouter_.getOctaveOffset());

        int transpose = settingsEl->getIntAttribute ("transpose", 0);
        int channel   = settingsEl->getIntAttribute ("channelFilter", 0);
        audioEngine_.setTranspose (transpose);
        audioEngine_.setChannelFilter (channel);
        if (auto* prefs = appProperties_.getUserSettings())
        {
            prefs->setValue ("transpose",     transpose);
            prefs->setValue ("channelFilter", channel);
        }

        double vol = settingsEl->getDoubleAttribute ("masterVolume", 1.0);
        if (onMasterVolumeChanged) onMasterVolumeChanged (vol);

        int coreW = settingsEl->getIntAttribute ("coreWindowW", 0);
        int coreH = settingsEl->getIntAttribute ("coreWindowH", 0);
        if (coreW > 200 && coreH > 100 && onCoreWindowBoundsChanged)
        {
            int coreX = settingsEl->getIntAttribute ("coreWindowX", 0);
            int coreY = settingsEl->getIntAttribute ("coreWindowY", 0);
            onCoreWindowBoundsChanged ({ coreX, coreY, coreW, coreH });
        }

        // Restore MIDI input device (match by identifier first, then by name)
        {
            juce::String savedId   = settingsEl->getStringAttribute ("midiInputIdentifier");
            juce::String savedName = settingsEl->getStringAttribute ("midiInputName");
            if (savedId.isNotEmpty() || savedName.isNotEmpty())
            {
                auto midiDevices = juce::MidiInput::getAvailableDevices();
                juce::String targetId;

                for (auto& d : midiDevices)
                    if (d.identifier == savedId) { targetId = d.identifier; break; }

                if (targetId.isEmpty() && savedName.isNotEmpty())
                    for (auto& d : midiDevices)
                        if (d.name == savedName) { targetId = d.identifier; break; }

                if (targetId.isNotEmpty())
                {
                    for (auto& d : midiDevices)
                        deviceManager_.setMidiInputDeviceEnabled (d.identifier, false);
                    deviceManager_.setMidiInputDeviceEnabled (targetId, true);
                    if (onMessage) onMessage ("MIDI IN restored: " + savedName);
                }
            }
        }

        // Restore Mixer Console visibility + position
        bool mixerWasVisible = settingsEl->getIntAttribute ("mixerVisible", 0) != 0;
        int mixerW = settingsEl->getIntAttribute ("mixerWindowW", 0);
        int mixerH = settingsEl->getIntAttribute ("mixerWindowH", 0);
        int mixerX = settingsEl->getIntAttribute ("mixerWindowX", 0);
        int mixerY = settingsEl->getIntAttribute ("mixerWindowY", 0);
        if (onMixerWindowRestored)
            onMixerWindowRestored (mixerWasVisible, { mixerX, mixerY, mixerW, mixerH });
    }

    // Restore MIDI routing state
    if (auto* routeEl = xml->getChildByName ("MidiRouting"))
    {
        bool allMode = routeEl->getIntAttribute ("routeToAll", 1) != 0;
        if (allMode)
            midiRouter_.setRouteToAll();
        else
            midiRouter_.setPendingTarget (routeEl->getStringAttribute ("targetPlugin"));
    }

    // Populate pending maps and launch bridges
    if (auto* bridgesEl = xml->getChildByName ("Bridges"))
    {
        for (auto* el : bridgesEl->getChildWithTagNameIterator ("Bridge"))
        {
            juce::String pluginPath = el->getStringAttribute ("plugin");
            juce::String roleStr    = el->getStringAttribute ("role", "instrument");
            auto role = (roleStr == "effect") ? BridgeInstance::Role::Effect
                                              : BridgeInstance::Role::Instrument;
            int x = el->getIntAttribute ("x", 0);
            int y = el->getIntAttribute ("y", 0);
            int w = el->getIntAttribute ("w", 0);
            int h = el->getIntAttribute ("h", 0);

            if (pluginPath.isNotEmpty())
            {
                if (w > 0 && h > 0)
                    pendingWindowBounds_[pluginPath] = { x, y, w, h };

                juce::String stateB64 = el->getStringAttribute ("state");
                if (stateB64.isNotEmpty())
                {
                    juce::MemoryBlock stateBytes;
                    juce::MemoryOutputStream mos (stateBytes, false);
                    juce::Base64::convertFromBase64 (mos, stateB64);
                    if (stateBytes.getSize() > 0)
                        pendingPluginStates_[pluginPath] = stateBytes;
                }

                MixerSettings ms;
                ms.gain     = (float) el->getDoubleAttribute ("gain",  1.0);
                ms.pan      = (float) el->getDoubleAttribute ("pan",   0.0);
                ms.muted    = el->getIntAttribute ("muted",    0) != 0;
                ms.bypassed = el->getIntAttribute ("bypassed", 0) != 0;
                ms.customName = el->getStringAttribute ("customName");
                juce::String colorStr = el->getStringAttribute ("customColor");
                if (colorStr.isNotEmpty())
                    ms.customColor = juce::Colour::fromString (colorStr);
                pendingMixerSettings_[pluginPath] = ms;

                juce::File pluginFile (pluginPath);
                if (pluginFile.exists())
                {
                    if (onLaunchBridge) onLaunchBridge (pluginFile, role);
                }
                else if (onMessage)
                {
                    onMessage ("Plugin not found: " + pluginFile.getFileName());
                }
            }
        }
    }

    if (onMessage) onMessage ("Project loaded: " + file.getFileNameWithoutExtension());
}
