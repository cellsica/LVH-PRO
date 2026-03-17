#pragma once
#include "UiCommon.h"

// =====================================================================
// InfoMonitorPanel — MIDI log, status indicators, DSP load
// =====================================================================
class InfoMonitorPanel : public Component, private Timer
{
public:
    std::function<double()> getCpuUsage; // set externally; called on message thread

    InfoMonitorPanel();

    void pushMidiMessage (const MidiMessage& m);
    void setTransposeDisplay (int semitones);
    void paint (Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildLog();

    TextEditor midiLog;
    Label channelLabel, transposeLabel, sustainLabel, cpuLabel;
    StringArray logLines;
    static constexpr int maxLogLines = 300;
    std::atomic<int>  pendingChannel  { 0 };
    std::atomic<bool> sustainPending  { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfoMonitorPanel)
};
