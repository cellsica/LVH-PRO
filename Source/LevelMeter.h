#pragma once
#include "UiCommon.h"

// =====================================================================
// LevelMeter — L/R bar display updated at ~30 Hz
// =====================================================================
class LevelMeter : public Component, private Timer
{
public:
    std::function<float(int)> getPeak; // set by application; called on message thread

    LevelMeter();

    void paint (Graphics& g) override;
    void timerCallback() override;

private:
    void paintBar (Graphics& g, Rectangle<int> area, float level);

    float displayL = 0.f, displayR = 0.f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};
