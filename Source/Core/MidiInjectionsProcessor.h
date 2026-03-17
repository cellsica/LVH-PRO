#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

using namespace juce;

// Injects MidiKeyboardState events into the MIDI buffer.
//
// Normal mode  : re-injects currently held keys every callback (injectKeyPresses=true)
//                so the virtual keyboard stays responsive.
// Panic mode   : sends AllSoundOff + AllNotesOff every callback for 500 ms.
//                Held-key re-injection is suppressed during this period.
//                Normal mode resumes automatically after the timeout.
// Transpose    : shifts injected note numbers by ±N semitones.
class MidiInjectionsProcessor : public AudioProcessor
{
public:
    MidiInjectionsProcessor (MidiKeyboardState& state)
        : AudioProcessor (BusesProperties()), keyboardState (state)
    {}

    // Called from the message thread on PANIC. Thread-safe via atomic.
    void setPanicMode()
    {
        panicEndTime.store (Time::getMillisecondCounterHiRes() + 500.0);
    }

    void setTranspose (int semitones) noexcept { transpose.store (semitones, std::memory_order_relaxed); }
    void setChannelFilter (int ch)    noexcept { channelFilter.store (ch,        std::memory_order_relaxed); }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override
    {
        if (Time::getMillisecondCounterHiRes() < panicEndTime.load())
        {
            keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), false);
            midiMessages.clear();
            for (int ch = 1; ch <= 16; ++ch)
            {
                midiMessages.addEvent (MidiMessage::allSoundOff (ch), 0);
                midiMessages.addEvent (MidiMessage::allNotesOff (ch), 0);
            }
            return;
        }

        int tp = transpose.load (std::memory_order_relaxed);

        if (tp == 0)
        {
            keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);
            return;
        }

        // With transpose: collect injected events and shift note numbers
        MidiBuffer injected;
        keyboardState.processNextMidiBuffer (injected, 0, buffer.getNumSamples(), true);
        for (auto meta : injected)
        {
            auto msg = meta.getMessage();
            if (msg.isNoteOn())
                msg = MidiMessage::noteOn  (msg.getChannel(),
                                            jlimit (0, 127, msg.getNoteNumber() + tp),
                                            msg.getVelocity());
            else if (msg.isNoteOff())
                msg = MidiMessage::noteOff (msg.getChannel(),
                                            jlimit (0, 127, msg.getNoteNumber() + tp));
            midiMessages.addEvent (msg, meta.samplePosition);
        }
    }

    const String getName() const override          { return "Midi Injection"; }
    double getTailLengthSeconds() const override   { return 0.0; }
    bool acceptsMidi() const override              { return false; }
    bool producesMidi() const override             { return true; }
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
    std::atomic<double> panicEndTime  { 0.0 };
    std::atomic<int>    transpose     { 0 };
    std::atomic<int>    channelFilter { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInjectionsProcessor)
};
