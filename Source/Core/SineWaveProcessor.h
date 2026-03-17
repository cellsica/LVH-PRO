#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

using namespace juce;

class SineWaveProcessor : public AudioProcessor
{
public:
    SineWaveProcessor (MidiKeyboardState& state)
        : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
          keyboardState (state)
    {}

    void prepareToPlay (double sampleRate, int) override { currentSampleRate = sampleRate; phase = 0.0; }
    void releaseResources() override {}

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override
    {
        keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);
        int activeNote = -1;
        for (int note = 127; note >= 0; --note)
            for (int ch = 1; ch <= 16; ++ch)
                if (keyboardState.isNoteOn (ch, note)) { activeNote = note; break; }
        if (activeNote != -1)
        {
            if (lastNote != activeNote)
            {
                phaseDelta = MidiMessage::getMidiNoteInHertz (activeNote) * 2.0 * MathConstants<double>::pi / currentSampleRate;
                lastNote = activeNote;
            }
            for (int s = 0; s < buffer.getNumSamples(); ++s)
            {
                auto v = (float) std::sin (phase) * 0.1f;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.setSample (ch, s, v);
                phase += phaseDelta;
            }
        }
        else { buffer.clear(); lastNote = -1; }
    }

    const String getName() const override          { return "Sine Wave"; }
    double getTailLengthSeconds() const override   { return 0.0; }
    bool acceptsMidi() const override              { return true; }
    bool producesMidi() const override             { return false; }
    AudioProcessorEditor* createEditor() override  { return nullptr; }
    bool hasEditor() const override                { return false; }
    int getNumPrograms() override                  { return 1; }
    int getCurrentProgram() override               { return 0; }
    void setCurrentProgram (int) override          {}
    const String getProgramName (int) override     { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override     {}
    void setStateInformation (const void*, int) override {}

private:
    MidiKeyboardState& keyboardState;
    double currentSampleRate = 44100.0, phase = 0.0, phaseDelta = 0.0;
    int lastNote = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SineWaveProcessor)
};
