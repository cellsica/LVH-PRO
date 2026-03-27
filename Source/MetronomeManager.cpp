#include "MetronomeManager.h"
#include "Core/AudioEngine.h"

// =========================================================================
// MetronomeProcessor
// =========================================================================

void MetronomeProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRate_            = sampleRate > 0.0 ? sampleRate : 44100.0;
    phaseAcc_              = 0.0;
    beatCount_             = 0;
    clickSamplesRemaining_ = 0;
    clickLenTotal_         = 0;
    clickPhase_            = 0.f;
    clickFreqCur_          = 1200.f;
    technoBarParity_       = false;
}

void MetronomeProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer&)
{
    if (! isPlaying_.load (std::memory_order_relaxed))
        return;

    const double bpm  = bpm_.load    (std::memory_order_relaxed);
    const float  vol  = volume_.load (std::memory_order_relaxed);
    const int    bpb  = beatsPerBar_.load (std::memory_order_relaxed);
    const int    ct   = clickType_.load   (std::memory_order_relaxed);

    const double samplesPerBeat = sampleRate_ * 60.0 / bpm;
    const double phaseInc       = 1.0 / samplesPerBeat;

    const int numCh   = juce::jmin (2, buffer.getNumChannels());
    const int numSamp = buffer.getNumSamples();

    for (int i = 0; i < numSamp; ++i)
    {
        // Advance phase; trigger click on wrap-around (= beat boundary)
        phaseAcc_ += phaseInc;
        if (phaseAcc_ >= 1.0)
        {
            phaseAcc_ -= 1.0;
            const int beatInBar   = beatCount_ % bpb;
            const bool isDownbeat = (beatInBar == 0);

            if (ct == 1)  // ── Techno (YMO CLICK) ─────────────────────────
            {
                if (isDownbeat)
                {
                    // Alternate "キ" / "カ" on every downbeat
                    if (! technoBarParity_)
                    {
                        clickFreqCur_  = 1480.f;                               // キ
                        clickLenTotal_ = static_cast<int> (sampleRate_ * 0.012);
                    }
                    else
                    {
                        clickFreqCur_  = 920.f;                                // カ
                        clickLenTotal_ = static_cast<int> (sampleRate_ * 0.016);
                    }
                    technoBarParity_ = ! technoBarParity_;
                }
                else
                {
                    clickFreqCur_  = 610.f;                                    // コ
                    clickLenTotal_ = static_cast<int> (sampleRate_ * 0.014);
                }
            }
            else           // ── Normal ────────────────────────────────────
            {
                clickFreqCur_  = isDownbeat ? 1200.f : 700.f;
                clickLenTotal_ = static_cast<int> (sampleRate_ * 0.025);
            }

            clickSamplesRemaining_ = clickLenTotal_;
            clickPhase_            = 0.f;
            ++beatCount_;

            // Notify message thread
            pendingBeat_.store (beatInBar, std::memory_order_relaxed);
            lastBeat_.store    (beatInBar, std::memory_order_relaxed);
            triggerAsyncUpdate();
        }

        // Generate click sample and MIX into buffer (pass-through + add)
        if (clickSamplesRemaining_ > 0)
        {
            const float t   = static_cast<float> (clickSamplesRemaining_)
                            / static_cast<float> (juce::jmax (1, clickLenTotal_));
            // Techno: quadratic (snappy) decay; Normal: linear decay
            const float env    = (ct == 1) ? (t * t) : t;
            const float sample = std::sin (clickPhase_) * vol * env;

            clickPhase_ += juce::MathConstants<float>::twoPi
                         * clickFreqCur_ / static_cast<float> (sampleRate_);
            if (clickPhase_ > juce::MathConstants<float>::twoPi)
                clickPhase_ -= juce::MathConstants<float>::twoPi;

            for (int ch = 0; ch < numCh; ++ch)
                buffer.addSample (ch, i, sample);

            --clickSamplesRemaining_;
        }
    }
}

void MetronomeProcessor::handleAsyncUpdate()
{
    if (onBeat)
        onBeat (pendingBeat_.load (std::memory_order_relaxed));
}

// =========================================================================
// MetronomeManager
// =========================================================================

MetronomeManager::MetronomeManager (AudioEngine& audioEngine)
    : audioEngine_ (audioEngine)
{}

void MetronomeManager::start()
{
    audioEngine_.setMetronomePlaying (true);
}

void MetronomeManager::stop()
{
    audioEngine_.setMetronomePlaying (false);
}

bool MetronomeManager::isPlaying() const noexcept
{
    return audioEngine_.isMetronomePlaying();
}

void MetronomeManager::setBpm (double bpm)
{
    audioEngine_.setMetronomeBpm (juce::jlimit (40.0, 240.0, bpm));
}

double MetronomeManager::getBpm() const noexcept
{
    return audioEngine_.getMetronomeBpm();
}

void MetronomeManager::setVolume (float v)
{
    audioEngine_.setMetronomeVolume (juce::jlimit (0.f, 1.f, v));
}

float MetronomeManager::getVolume() const noexcept
{
    return audioEngine_.getMetronomeVolume();
}

void MetronomeManager::setBeatsPerBar (int b)
{
    audioEngine_.setMetronomeBeatsPerBar (juce::jlimit (1, 16, b));
}

int MetronomeManager::getBeatsPerBar() const noexcept
{
    return audioEngine_.getMetronomeBeatsPerBar();
}

void MetronomeManager::tap()
{
    const juce::int64 now = juce::Time::currentTimeMillis();
    tapTimes_.push_back (now);

    // Discard taps older than 3 seconds, and keep at most 8
    while (tapTimes_.size() > 8
           || (tapTimes_.size() > 1 && now - tapTimes_.front() > 3000LL))
        tapTimes_.pop_front();

    if (tapTimes_.size() < 2)
        return;

    // Average interval across all retained taps
    const double totalMs = static_cast<double> (tapTimes_.back() - tapTimes_.front());
    const double avgMs   = totalMs / static_cast<double> (tapTimes_.size() - 1);
    setBpm (60000.0 / avgMs);
}
