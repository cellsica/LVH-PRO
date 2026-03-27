#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <deque>

// =========================================================================
// MetronomeProcessor
//
// AudioProcessor node that generates metronome click audio and mixes it
// into the signal passing through it (pass-through + add).
// Insert between GainMeter and Output nodes in the AudioProcessorGraph.
//
// All audio-thread operations use only std::atomic — no locks.
// Beat notifications reach the message thread via juce::AsyncUpdater.
// =========================================================================
class MetronomeProcessor : public juce::AudioProcessor,
                           public juce::AsyncUpdater
{
public:
    MetronomeProcessor()
        : juce::AudioProcessor (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {}

    ~MetronomeProcessor() override { cancelPendingUpdate(); }

    // ── Thread-safe setters (message thread) ──────────────────────────────
    void setPlaying      (bool p)   noexcept { isPlaying_.store   (p,   std::memory_order_relaxed); }
    void setBpm          (double b) noexcept { bpm_.store         (b,   std::memory_order_relaxed); }
    void setVolume       (float v)  noexcept { volume_.store      (v,   std::memory_order_relaxed); }
    void setBeatsPerBar  (int n)    noexcept { beatsPerBar_.store (n,   std::memory_order_relaxed); }

    bool   isPlaying()    const noexcept { return isPlaying_.load  (std::memory_order_relaxed); }
    double getBpm()       const noexcept { return bpm_.load        (std::memory_order_relaxed); }
    float  getVolume()    const noexcept { return volume_.load     (std::memory_order_relaxed); }
    int    getBeatsPerBar() const noexcept { return beatsPerBar_.load (std::memory_order_relaxed); }

    // Current beat index (0 = downbeat). Written on audio thread, read on message thread.
    int  getCurrentBeat() const noexcept { return lastBeat_.load (std::memory_order_relaxed); }

    // Fired on message thread when a new beat fires. Parameter: beat index (0 = downbeat).
    std::function<void(int beat)> onBeat;

    // ── AudioProcessor interface ──────────────────────────────────────────
    const juce::String getName() const override               { return "LVH Metronome"; }
    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    void processBlock  (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    double getTailLengthSeconds() const override        { return 0.0; }
    bool   acceptsMidi()          const override        { return false; }
    bool   producesMidi()         const override        { return false; }
    bool   hasEditor()            const override        { return false; }
    juce::AudioProcessorEditor* createEditor()  override { return nullptr; }
    int  getNumPrograms()         override              { return 1; }
    int  getCurrentProgram()      override              { return 0; }
    void setCurrentProgram (int)  override              {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&)       override {}
    void setStateInformation (const void*, int)         override {}

private:
    void handleAsyncUpdate() override;

    // ── Atomics (audio thread ↔ message thread) ───────────────────────────
    std::atomic<bool>   isPlaying_   { false };
    std::atomic<double> bpm_         { 120.0 };
    std::atomic<float>  volume_      { 0.7f  };
    std::atomic<int>    beatsPerBar_ { 4     };
    std::atomic<int>    lastBeat_    { 0     };   // written audio thread
    std::atomic<int>    pendingBeat_ { 0     };   // payload for AsyncUpdater

    // ── Audio-thread only (no sync needed) ───────────────────────────────
    double sampleRate_             = 44100.0;
    double phaseAcc_               = 0.0;    // 0.0 → 1.0 per beat
    int    beatCount_              = 0;      // total beats since last start
    int    clickSamplesRemaining_  = 0;
    float  clickPhase_             = 0.f;
    bool   isHiBeat_               = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomeProcessor)
};

// =========================================================================
// MetronomeManager
//
// Message-thread API wrapper around MetronomeProcessor.
// Provides BPM management, start/stop, tap tempo, and beat callbacks.
//
// MetronomeProcessor is created and owned by AudioEngine (same pattern as
// GainAndMeterProcessor). MetronomeManager communicates with it via
// AudioEngine's pending-state methods.
// =========================================================================
class AudioEngine;   // forward declaration — full def in AudioEngine.h

class MetronomeManager
{
public:
    explicit MetronomeManager (AudioEngine& audioEngine);

    // ── Playback control ──────────────────────────────────────────────────
    void start();
    void stop();
    void toggle() { if (isPlaying()) stop(); else start(); }
    bool isPlaying() const noexcept;

    // ── BPM (clamped to 40–240) ───────────────────────────────────────────
    void   setBpm (double bpm);
    double getBpm() const noexcept;

    // ── Volume (0.0–1.0) ──────────────────────────────────────────────────
    void  setVolume (float v);
    float getVolume() const noexcept;

    // ── Time signature ────────────────────────────────────────────────────
    void setBeatsPerBar (int b);
    int  getBeatsPerBar() const noexcept;

    // ── Tap tempo ─────────────────────────────────────────────────────────
    // Call on each tap; BPM updates automatically after 2+ taps.
    // Taps older than 3 seconds are discarded.
    void tap();

    // ── Beat notification (fired on message thread) ───────────────────────
    // Set this before start(). Parameter: beat index (0 = downbeat).
    std::function<void(int beat)> onBeat;

private:
    AudioEngine&          audioEngine_;
    std::deque<juce::int64> tapTimes_;   // tap timestamps in milliseconds

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomeManager)
};
