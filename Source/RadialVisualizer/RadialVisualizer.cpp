// =============================================================================
// LVH RadialVisualizer v2
//
// Visual layers (back to front):
//   1. Background radial gradient
//   2. Wave rings — FFT-shaped rings expanding outward, Doppler-coloured
//   3. Radial bars — current spectrum, magnitude-coloured (cyan → red)
//   4. Nucleus — glowing core + orbiting particles
// =============================================================================

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_opengl/juce_opengl.h>
#include <array>
#include <deque>
#include "../VisualizerSDK/IVisualizerPlugin.h"
#include "../VisualizerSDK/IAudioSource.h"

class RadialVisualizer final : public IVisualizerPlugin
{
public:
    static constexpr int   kBins         = 512;
    static constexpr int   kRingBins     = 256;   // sampling resolution for rings
    static constexpr float kEmitSpeed     = 2.0f;  // pixels per frame
    static constexpr int   kEmitInterval  = 3;     // emit new ring every N frames
    static constexpr int   kMaxRings      = 45;
    static constexpr float kEmitThreshold = 0.08f; // 全帯域合計がこれ以下なら無音とみなす

    // ── Wave ring snapshot ─────────────────────────────────────────────────
    struct WaveRing
    {
        std::array<float, kRingBins> mag {};   // downsampled FFT snapshot
        float baseRadius = 0.f;                // current expansion offset
    };

    // ── Orbiting nucleus particle ──────────────────────────────────────────
    struct Particle
    {
        float orbitRadius;
        float speed;       // radians per frame (signed → CW or CCW)
        float phase;       // current angle
        float dotSize;
    };

    // ── Lifecycle ──────────────────────────────────────────────────────────

    void initialise (IAudioSource* source) override
    {
        source_ = source;
        std::fill (smoothed_.begin(), smoothed_.end(), 0.f);
        frameCount_ = 0;
        rings_.clear();

        // Five particles on different orbital radii / speeds / phases
        particles_[0] = {  9.f,  0.047f, 0.000f, 2.8f };
        particles_[1] = { 14.f, -0.031f, 2.094f, 2.2f };
        particles_[2] = {  6.f,  0.073f, 4.189f, 2.0f };
        particles_[3] = { 12.f, -0.055f, 1.047f, 1.8f };
        particles_[4] = {  7.f,  0.089f, 3.665f, 1.5f };
    }

    // ── Render ─────────────────────────────────────────────────────────────

    void render (juce::Graphics& g, juce::OpenGLContext*) override
    {
        if (source_ == nullptr) return;

        // Fetch + IIR smooth
        float raw[kBins] = {};
        source_->getFFTData (raw, kBins);
        for (int i = 0; i < kBins; ++i)
            smoothed_[i] = smoothed_[i] * 0.72f + raw[i] * 0.28f;

        const auto  bounds = g.getClipBounds().toFloat();
        const float cx     = bounds.getCentreX();
        const float cy     = bounds.getCentreY();
        const float maxR   = std::min (bounds.getWidth(), bounds.getHeight()) * 0.48f;
        const float scaleY = maxR * 3.2f;   // FFT magnitude → pixels

        // Low-mid energy for nucleus pulse (bins 2-48)
        float energy = 0.f;
        for (int i = 2; i < 48; ++i) energy += smoothed_[i];
        energy = juce::jmin (energy * 5.f, 1.f);

        const float coreR = 14.f + energy * 7.f;   // nucleus radius

        // ── 1. Background ─────────────────────────────────────────────
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff0c0c1e), cx, cy,
            juce::Colour (0xff020207), cx + maxR, cy + maxR,
            true));
        g.fillRect (bounds);

        // ── 2. Wave rings ─────────────────────────────────────────────
        ++frameCount_;

        // 全帯域エネルギーを合算してしきい値判定（無音時はリングを発射しない）
        float totalEnergy = 0.f;
        for (int i = 0; i < kBins; ++i) totalEnergy += smoothed_[i];

        if (frameCount_ % kEmitInterval == 0
            && totalEnergy > kEmitThreshold
            && (int)rings_.size() < kMaxRings)
        {
            WaveRing ring;
            for (int k = 0; k < kRingBins; ++k)
                ring.mag[k] = (smoothed_[k * 2] + smoothed_[k * 2 + 1]) * 0.5f;
            ring.baseRadius = coreR + 2.f;
            rings_.push_back (ring);
        }

        for (auto it = rings_.begin(); it != rings_.end(); )
        {
            it->baseRadius += kEmitSpeed;
            const float progress = it->baseRadius / maxR;   // 0→1
            const float alpha    = (1.f - progress) * 0.70f;

            if (progress >= 1.02f || alpha < 0.015f)
            {
                it = rings_.erase (it);
                continue;
            }

            drawWaveRing (g, cx, cy, *it, scaleY, alpha, progress);
            ++it;
        }

        // ── 3. Radial bars ────────────────────────────────────────────
        drawRadialBars (g, cx, cy, coreR, maxR, scaleY);

        // ── 4. Nucleus ────────────────────────────────────────────────
        drawNucleus (g, cx, cy, coreR, energy);
    }

    void shutdown() override { source_ = nullptr; }

private:
    // ── Wave ring drawing ──────────────────────────────────────────────────
    //
    // Each ring is a closed smooth path.  The radial position of each point
    // is: baseRadius + fftMag * scaleY.
    // Doppler colour: progress 0 (near centre, just emitted) = red (hue 0),
    //                 progress 1 (outer edge, old)            = violet (hue 0.72)
    void drawWaveRing (juce::Graphics& g,
                       float cx, float cy,
                       const WaveRing& ring,
                       float scaleY, float alpha, float progress)
    {
        juce::Path path;

        for (int i = 0; i <= kRingBins; ++i)
        {
            const int   idx   = i % kRingBins;
            const float angle = (float)idx / (float)kRingBins
                                * juce::MathConstants<float>::twoPi
                                - juce::MathConstants<float>::halfPi;

            const float r  = ring.baseRadius + ring.mag[idx] * scaleY;
            const float px = cx + r * std::cos (angle);
            const float py = cy + r * std::sin (angle);

            if (i == 0) path.startNewSubPath (px, py);
            else        path.lineTo (px, py);
        }
        path.closeSubPath();

        // Doppler hue shift
        const float hue = progress * 0.72f;
        const float sat = 0.85f + (1.f - progress) * 0.10f;  // slightly more saturated near centre
        const float val = 0.95f;
        g.setColour (juce::Colour::fromHSV (hue, sat, val, alpha));
        g.strokePath (path, juce::PathStrokeType (1.3f));
    }

    // ── Radial bars ────────────────────────────────────────────────────────
    //
    // 512 bars from inner radius (nucleus edge) outward.
    // Colour: magnitude high = red (hue 0), low = cyan (hue 0.5)
    void drawRadialBars (juce::Graphics& g,
                         float cx, float cy,
                         float innerR, float maxR, float scaleY)
    {
        for (int i = 0; i < kBins; ++i)
        {
            const float mag    = smoothed_[i];
            const float barLen = juce::jmin (mag * scaleY, maxR - innerR);
            if (barLen < 0.4f) continue;

            const float angle = (float)i / (float)kBins
                                * juce::MathConstants<float>::twoPi
                                - juce::MathConstants<float>::halfPi;
            const float cosA  = std::cos (angle);
            const float sinA  = std::sin (angle);

            // Cyan (low) → red (high)
            const float t   = juce::jmin (mag * 10.f, 1.f);
            const float hue = (1.f - t) * 0.5f;   // 0.5 = cyan, 0.0 = red
            g.setColour (juce::Colour::fromHSV (hue, 0.9f, 1.0f, 0.80f + t * 0.15f));

            g.drawLine (cx + innerR * cosA, cy + innerR * sinA,
                        cx + (innerR + barLen) * cosA,
                        cy + (innerR + barLen) * sinA,
                        1.5f);
        }
    }

    // ── Nucleus drawing ────────────────────────────────────────────────────
    //
    // Layers (back to front):
    //   a. Outer soft glow halos (large, very transparent)
    //   b. Core filled disc (semi-transparent cyan)
    //   c. Bright centre highlight
    //   d. 5 orbiting particles with glow
    void drawNucleus (juce::Graphics& g,
                      float cx, float cy,
                      float coreR, float energy)
    {
        // a. Glow halos
        for (int layer = 5; layer >= 1; --layer)
        {
            const float r  = coreR + (float)layer * 5.5f;
            const float al = 0.12f / (float)layer;
            g.setColour (juce::Colour::fromHSV (0.58f, 0.75f, 1.0f, al));
            g.fillEllipse (cx - r, cy - r, r * 2.f, r * 2.f);
        }

        // b. Core disc
        g.setColour (juce::Colour::fromHSV (0.60f, 0.55f, 0.95f, 0.75f));
        g.fillEllipse (cx - coreR, cy - coreR, coreR * 2.f, coreR * 2.f);

        // c. Centre highlight
        const float cr = coreR * 0.38f;
        g.setColour (juce::Colours::white.withAlpha (0.88f));
        g.fillEllipse (cx - cr, cy - cr, cr * 2.f, cr * 2.f);

        // d. Orbiting particles
        for (auto& p : particles_)
        {
            p.phase += p.speed * (1.f + energy * 0.45f);  // speed up with energy

            const float pr = p.orbitRadius * (1.f + energy * 0.25f);
            const float px = cx + pr * std::cos (p.phase);
            const float py = cy + pr * std::sin (p.phase);

            // Glow halo around particle
            const float gr = p.dotSize * 2.8f;
            g.setColour (juce::Colour::fromHSV (0.56f, 0.85f, 1.0f, 0.30f));
            g.fillEllipse (px - gr, py - gr, gr * 2.f, gr * 2.f);

            // Particle dot
            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.fillEllipse (px - p.dotSize * 0.65f,
                           py - p.dotSize * 0.65f,
                           p.dotSize * 1.3f,
                           p.dotSize * 1.3f);
        }
    }

    // ── State ──────────────────────────────────────────────────────────────
    IAudioSource*              source_     = nullptr;
    std::array<float, kBins>   smoothed_   {};
    std::deque<WaveRing>       rings_;
    std::array<Particle, 5>    particles_  {};
    int                        frameCount_ = 0;
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
