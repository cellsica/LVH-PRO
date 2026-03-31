// =============================================================================
// LVH TestVisualizer — Minimal SDK-compliant visualizer DLL
//
// Renders a simple FFT bar-graph on the provided Graphics context, and logs
// waveform RMS to confirm that audio data is flowing from the host.
// Build with the LVH-TestVisualizer CMake target.
// =============================================================================

// Include JUCE modules directly (no JuceHeader.h — this is a standalone DLL).
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_opengl/juce_opengl.h>
#include "../VisualizerSDK/IVisualizerPlugin.h"
#include "../VisualizerSDK/IAudioSource.h"

class TestVisualizer final : public IVisualizerPlugin
{
public:
    // -------------------------------------------------------------------------
    void initialise (IAudioSource* source) override
    {
        source_ = source;
        DBG ("[TestVisualizer] initialise() — sampleRate=" +
             juce::String (source ? source->getSampleRate() : 0.0));
    }

    // -------------------------------------------------------------------------
    void render (juce::Graphics& g, juce::OpenGLContext* /*openGLContext*/) override
    {
        if (source_ == nullptr) return;

        constexpr int kBins = 512;
        float fft[kBins] = {};
        source_->getFFTData (fft, kBins);

        // ── Confirm data is arriving (log every ~120 frames) ──────────────
        ++frameCount_;
        if ((frameCount_ % 120) == 0)
        {
            float sum = 0.f;
            for (int i = 0; i < kBins; ++i) sum += fft[i];
            DBG ("[TestVisualizer] FFT sum=" + juce::String (sum, 4) +
                 "  bpm=" + juce::String (source_->getBPM(), 1));
        }

        // ── Draw bar graph ─────────────────────────────────────────────────
        auto bounds = g.getClipBounds().toFloat();
        g.fillAll (juce::Colour (0xff0a0a14));  // dark background

        const float barW   = bounds.getWidth()  / (float)kBins;
        const float maxH   = bounds.getHeight();
        const float scaleY = maxH * 5.0f;       // scale so typical signals look good

        for (int i = 0; i < kBins; ++i)
        {
            const float h = juce::jmin (fft[i] * scaleY, maxH);
            if (h < 0.5f) continue;

            // Colour gradient: low freq = cyan, high = magenta
            const float t = (float)i / (float)(kBins - 1);
            auto col = juce::Colour::fromHSV (0.5f + t * 0.3f, 0.9f, 0.9f, 1.0f);
            g.setColour (col);
            g.fillRect (bounds.getX() + i * barW,
                        bounds.getBottom() - h,
                        barW,
                        h);
        }
    }

    // -------------------------------------------------------------------------
    void shutdown() override
    {
        DBG ("[TestVisualizer] shutdown()");
        source_ = nullptr;
    }

private:
    IAudioSource* source_     = nullptr;
    int           frameCount_ = 0;
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
        return new TestVisualizer();
    }
}
