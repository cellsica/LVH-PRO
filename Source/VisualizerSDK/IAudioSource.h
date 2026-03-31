#pragma once

// =============================================================================
// IAudioSource  — LVH Visualizer SDK  (Phase A)
//
// Implemented by VisualizerManager and passed to each IVisualizerPlugin on
// initialise().  Provides real-time audio analysis data so that visualizer
// plugins do not need to touch any JUCE audio internals directly.
// =============================================================================
class IAudioSource
{
public:
    virtual ~IAudioSource() = default;

    // -------------------------------------------------------------------------
    // FFT magnitude spectrum
    //   - Returns the latest magnitude spectrum computed from the master output.
    //   - `size` is the number of bins requested (typically FFT_SIZE / 2 = 512).
    //   - Values are normalised (0.0 – 1.0 approximate range; may exceed 1.0
    //     for very loud signals).
    // -------------------------------------------------------------------------
    virtual void getFFTData (float* buffer, int size) = 0;

    // -------------------------------------------------------------------------
    // Raw waveform (time-domain)
    //   - Returns the latest mono-summed PCM block (mixed master output).
    //   - `size` is the number of samples requested (typically FFT_SIZE = 1024).
    //   - Values are in the range [-1.0, 1.0].
    // -------------------------------------------------------------------------
    virtual void getWaveformData (float* buffer, int size) = 0;

    // -------------------------------------------------------------------------
    // Metadata
    // -------------------------------------------------------------------------
    virtual double getSampleRate() const = 0;   // e.g. 44100.0 / 48000.0
    virtual double getBPM()        const = 0;   // current metronome BPM
};
