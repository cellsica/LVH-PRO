#pragma once

/**
 * @file IAudioSource.h
 * @brief LVH Visualizer SDK — audio analysis data interface.
 */

/**
 * @class IAudioSource
 * @brief Provides real-time audio analysis data to visualizer plugins.
 *
 * Implemented by VisualizerManager and passed to each IVisualizerPlugin via
 * IVisualizerPlugin::initialise().  Plugins call these methods every frame to
 * obtain the latest FFT spectrum, waveform, and transport metadata without
 * depending on any JUCE audio internals directly.
 *
 * All methods are called from the message thread (same thread as render()).
 * The implementation copies data from the audio thread using a SpinLock.
 */
class IAudioSource
{
public:
    virtual ~IAudioSource() = default;

    /**
     * @brief Fill @p buffer with the latest FFT magnitude spectrum.
     *
     * Returns the magnitude spectrum computed from the master mix output.
     * Values are approximately normalised to [0.0, 1.0] but may exceed 1.0
     * for very loud signals.
     *
     * @param buffer  Caller-allocated array of at least @p size floats.
     * @param size    Number of frequency bins to write (typically 512,
     *                i.e. FFT_SIZE / 2).
     */
    virtual void getFFTData (float* buffer, int size) = 0;

    /**
     * @brief Fill @p buffer with the latest time-domain waveform samples.
     *
     * Returns a mono-summed PCM block of the master mix output.
     * Sample values are in the range [-1.0, 1.0].
     *
     * @param buffer  Caller-allocated array of at least @p size floats.
     * @param size    Number of samples to write (typically 1024, i.e. FFT_SIZE).
     */
    virtual void getWaveformData (float* buffer, int size) = 0;

    /**
     * @brief Returns the current audio sample rate.
     * @return Sample rate in Hz (e.g. 44100.0, 48000.0).
     */
    virtual double getSampleRate() const = 0;

    /**
     * @brief Returns the current metronome tempo.
     * @return BPM value as set in the LVH metronome (e.g. 120.0).
     */
    virtual double getBPM() const = 0;
};
