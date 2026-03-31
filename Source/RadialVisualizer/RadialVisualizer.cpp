// =============================================================================
// LVH RadialVisualizer — Circular FFT spectrum analyzer
//
// 512 FFT magnitude bins are displayed as radial bars arranged around a circle.
// IIR smoothing (α = 0.3 per frame at 60 fps) gives natural, fluid motion.
// =============================================================================

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_opengl/juce_opengl.h>
#include "../VisualizerSDK/IVisualizerPlugin.h"
#include "../VisualizerSDK/IAudioSource.h"

class RadialVisualizer final : public IVisualizerPlugin
{
public:
    static constexpr int kBins = 512;

    // -------------------------------------------------------------------------
    void initialise (IAudioSource* source) override
    {
        source_ = source;
        std::fill (smoothed_.begin(), smoothed_.end(), 0.f);
        DBG ("[RadialVisualizer] initialise() — sampleRate=" +
             juce::String (source ? source->getSampleRate() : 0.0));
    }

    // -------------------------------------------------------------------------
    void render (juce::Graphics& g, juce::OpenGLContext* /*openGLContext*/) override
    {
        if (source_ == nullptr) return;

        // ── Fetch FFT data ─────────────────────────────────────────────────
        float raw[kBins] = {};
        source_->getFFTData (raw, kBins);

        // ── IIR smoothing  val = val * 0.7 + newSample * 0.3 ──────────────
        for (int i = 0; i < kBins; ++i)
            smoothed_[i] = smoothed_[i] * 0.7f + raw[i] * 0.3f;

        // ── Draw ───────────────────────────────────────────────────────────
        auto bounds = g.getClipBounds().toFloat();
        g.fillAll (juce::Colour (0xff07070f));  // near-black background

        const float cx   = bounds.getCentreX();
        const float cy   = bounds.getCentreY();
        const float minR = std::min (bounds.getWidth(), bounds.getHeight()) * 0.12f;
        const float maxR = std::min (bounds.getWidth(), bounds.getHeight()) * 0.46f;
        const float barScale = (maxR - minR) * 6.0f;  // boost for visibility

        // Inner circle
        g.setColour (juce::Colour (0x40ffffff));
        g.drawEllipse (cx - minR, cy - minR, minR * 2.f, minR * 2.f, 1.0f);

        // Radial bars
        for (int i = 0; i < kBins; ++i)
        {
            const float angle = (float)i / (float)kBins
                                * juce::MathConstants<float>::twoPi
                                - juce::MathConstants<float>::halfPi; // start at top

            const float barLen = juce::jmin (smoothed_[i] * barScale, maxR - minR);
            if (barLen < 0.3f) continue;

            const float r0 = minR;
            const float r1 = minR + barLen;

            const float cosA = std::cos (angle);
            const float sinA = std::sin (angle);

            // Colour: hue sweeps full spectrum around the circle
            float hue = (float)i / (float)kBins;
            auto col  = juce::Colour::fromHSV (hue, 0.85f, 1.0f, 1.0f);
            g.setColour (col);

            g.drawLine (cx + r0 * cosA,
                        cy + r0 * sinA,
                        cx + r1 * cosA,
                        cy + r1 * sinA,
                        1.5f);
        }

        // Centre glow dot
        const float glow = juce::jmin (smoothed_[1] * 200.f, 1.f);
        g.setColour (juce::Colour::fromFloatRGBA (0.4f, 0.8f, 1.0f, 0.6f + glow * 0.4f));
        g.fillEllipse (cx - 4.f, cy - 4.f, 8.f, 8.f);

        // ── Periodic data-arrival log (every 5 seconds at 60 fps) ─────────
        ++frameCount_;
        if ((frameCount_ % 300) == 0)
        {
            float sum = 0.f;
            for (int i = 0; i < kBins; ++i) sum += smoothed_[i];
            DBG ("[RadialVisualizer] FFT energy=" + juce::String (sum, 4) +
                 "  bpm=" + juce::String (source_->getBPM(), 1));
        }
    }

    // -------------------------------------------------------------------------
    void shutdown() override
    {
        DBG ("[RadialVisualizer] shutdown()");
        source_ = nullptr;
    }

private:
    IAudioSource*               source_     = nullptr;
    std::array<float, kBins>    smoothed_   {};
    int                         frameCount_ = 0;
};

// =============================================================================
// DLL entry point
// =============================================================================
extern "C"
{
#ifdef _WIN32
    __declspec(dllexport)
#endif
    IVisualizerPlugin* createVisualizer()
    {
        return new RadialVisualizer();
    }
}
