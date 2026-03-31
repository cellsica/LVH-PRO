// StarfieldVisualizer.cpp
// LVH Visualizer SDK Plugin — Warp-speed starfield driven by FFT data
//
// Doppler coloring: far stars = blue → mid = white → near stars = red
// Bass  → warp speed burst + new star generation
// Treble → brightness sparkle on each star
//
// Architecture:
//   - 800 Star particles in a fixed pool (no heap alloc per frame)
//   - z-axis warp projection: far(z=1) → near(z≈0), projected as x/z, y/z
//   - Motion streak drawn for near stars (progress > 0.55)

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../VisualizerSDK/IVisualizerPlugin.h"
#include <array>
#include <cmath>
#include <random>

// ─── Constants ────────────────────────────────────────────────────────────────
static constexpr int   kMaxStars      = 800;
static constexpr int   kInitStars     = 400;
static constexpr int   kFFTBins       = 512;
static constexpr float kBaseSpeed     = 0.016f;   // z decrement per frame at silence
static constexpr float kMaxSpeedMul   = 7.0f;     // max speed multiplier on bass hit
static constexpr float kZNear         = 0.04f;    // stars beyond this are recycled
static constexpr float kZFar          = 1.0f;
static constexpr int   kBassBinCount  = 18;       // FFT bins 0..17 → kick/bass
static constexpr int   kTrebleBinStart= 90;       // FFT bins 90..511 → treble
static constexpr float kBassNormRef   = 0.0008f;  // bass raw value → normalised 1.0
static constexpr float kTrebleNormRef = 0.0003f;

// ─── Star particle ─────────────────────────────────────────────────────────────
struct Star
{
    float x = 0.f, y = 0.f;   // radial position (-1..1 normalised)
    float z = 1.f;             // depth: 1=far, 0=near camera
    float size = 1.5f;         // base dot radius
    float hueOffset = 0.f;     // small random hue variation (±0.05)
    bool  active    = false;
};

// ─── Visualizer ───────────────────────────────────────────────────────────────
class StarfieldVisualizer : public IVisualizerPlugin
{
public:
    StarfieldVisualizer() : rng_ (std::random_device{}()) {}

    void initialise (IAudioSource* source) override
    {
        source_ = source;
        for (auto& s : stars_) s.active = false;
        for (int i = 0; i < kInitStars; ++i)
            spawnStar (false /*scatter across depths*/);
    }

    void render (juce::Graphics& g, juce::OpenGLContext*) override
    {
        if (source_ == nullptr) return;

        // ── FFT analysis ──────────────────────────────────────────────
        std::array<float, kFFTBins> fft {};
        source_->getFFTData (fft.data(), kFFTBins);

        float bassRaw = 0.f;
        for (int i = 0; i < kBassBinCount; ++i)
            bassRaw += fft[i];
        bassRaw /= (float)kBassBinCount;

        float trebleRaw = 0.f;
        for (int i = kTrebleBinStart; i < kFFTBins; ++i)
            trebleRaw += fft[i];
        trebleRaw /= (float)(kFFTBins - kTrebleBinStart);

        bassSmooth_   = bassSmooth_   * 0.72f + bassRaw   * 0.28f;
        trebleSmooth_ = trebleSmooth_ * 0.80f + trebleRaw * 0.20f;

        // Normalised 0..1 energies  (sqrt for perceptual scaling)
        const float bassNorm   = juce::jmin (1.f, std::sqrt (bassSmooth_   / kBassNormRef));
        const float trebleNorm = juce::jmin (1.f, std::sqrt (trebleSmooth_ / kTrebleNormRef));

        // Flash intensity (strong bass hit → brief global brightness)
        flashAlpha_ = flashAlpha_ * 0.85f + bassNorm * bassNorm * 0.15f;

        // ── Background ────────────────────────────────────────────────
        {
            // Slight nebula glow on bass
            const float glow = bassNorm * 0.18f;
            juce::Colour bg = juce::Colour (0xff05050f)
                                  .interpolatedWith (juce::Colour (0xff0d0535), glow);
            g.fillAll (bg);
        }

        auto  bounds = g.getClipBounds().toFloat();
        float cx     = bounds.getCentreX();
        float cy     = bounds.getCentreY();
        float scale  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.50f;

        // ── Speed this frame ──────────────────────────────────────────
        float speed = kBaseSpeed * (1.f + bassNorm * (kMaxSpeedMul - 1.f));

        // ── Bass burst: spawn new stars on strong beat ─────────────────
        if (bassNorm > 0.35f)
        {
            int burst = juce::roundToInt ((bassNorm - 0.35f) / 0.65f * 18.f);
            for (int i = 0; i < burst; ++i)
                spawnStar (true /*near z=far, tightly clustered*/);
        }

        // ── Update + draw each star ────────────────────────────────────
        std::uniform_real_distribution<float> sparkleDist (0.85f, 1.15f);

        for (auto& s : stars_)
        {
            if (!s.active) continue;

            s.z -= speed;

            if (s.z < kZNear)
            {
                s.active = false;
                continue;
            }

            // 2D projection (perspective divide)
            const float sx = cx + (s.x / s.z) * scale;
            const float sy = cy + (s.y / s.z) * scale;

            // Cull off-screen
            if (sx < -4.f || sx > bounds.getWidth() + 4.f ||
                sy < -4.f || sy > bounds.getHeight() + 4.f)
            {
                s.active = false;
                continue;
            }

            // progress: 0=far/new, 1=very close
            const float progress = 1.f - (s.z / kZFar);

            // Dot radius — grows as star approaches + treble sparkle
            float r = s.size * (0.3f + progress * 1.5f)
                      * (1.f + trebleNorm * 1.8f * sparkleDist (rng_));
            r = juce::jmax (0.4f, juce::jmin (r, 9.f));

            // ── Doppler coloring ──────────────────────────────────────
            // far  (progress≈0) → blue  (hue 0.60)
            // mid  (progress≈0.5) → white (sat→0)
            // near (progress≈1)  → red   (hue 0.0)
            float hue, sat, bri;
            if (progress < 0.5f)
            {
                const float t = progress * 2.f;          // 0..1
                hue = 0.60f + s.hueOffset;               // blue band
                sat = 1.f - t * 0.95f;                   // saturated → near-white
                bri = 0.35f + t * 0.65f;                 // dim → bright
            }
            else
            {
                const float t = (progress - 0.5f) * 2.f; // 0..1
                hue = 0.02f * s.hueOffset;                // slight red variation
                sat = t * 0.95f;                          // white → red
                bri = 1.0f;
            }
            // Treble boosts overall brightness
            bri = juce::jmin (1.f, bri + trebleNorm * 0.35f);
            // Alpha: ramp from near-invisible (far) to opaque (near)
            const float alpha = juce::jmin (1.f, 0.12f + progress * 0.88f);

            const juce::Colour col = juce::Colour::fromHSV (
                juce::jmax (0.f, juce::jmin (1.f, hue)), sat, bri, alpha);

            // ── Motion streak (near stars only) ──────────────────────
            if (progress > 0.55f)
            {
                const float prevZ  = s.z + speed * 4.f;
                const float px     = cx + (s.x / prevZ) * scale;
                const float py     = cy + (s.y / prevZ) * scale;
                const float streakW = r * 0.55f;
                g.setColour (col.withAlpha (alpha * 0.28f));
                g.drawLine (px, py, sx, sy, streakW);
            }

            // ── Core dot ─────────────────────────────────────────────
            g.setColour (col);
            g.fillEllipse (sx - r, sy - r, r * 2.f, r * 2.f);

            // Inner bright core (near only)
            if (progress > 0.7f)
            {
                g.setColour (juce::Colours::white.withAlpha (alpha * 0.55f));
                const float cr = r * 0.35f;
                g.fillEllipse (sx - cr, sy - cr, cr * 2.f, cr * 2.f);
            }
        }

        // ── Flash overlay (strong bass hit) ──────────────────────────
        if (flashAlpha_ > 0.01f)
        {
            g.setColour (juce::Colour (0xffffffff).withAlpha (
                juce::jmin (0.18f, flashAlpha_ * 0.18f)));
            g.fillAll();
        }

        // ── Pool maintenance: keep ~400 active stars ──────────────────
        {
            int active = 0;
            for (auto& s : stars_) if (s.active) ++active;
            while (active < 400)
            {
                spawnStar (false);
                ++active;
            }
        }
    }

    void shutdown() override { source_ = nullptr; }

    int getPreferredWidth()  const override { return 520; }
    int getPreferredHeight() const override { return 520; }

private:
    // ── State ─────────────────────────────────────────────────────────
    IAudioSource*           source_       = nullptr;
    std::array<Star, kMaxStars> stars_    {};
    std::mt19937            rng_;
    float                   bassSmooth_   = 0.f;
    float                   trebleSmooth_ = 0.f;
    float                   flashAlpha_   = 0.f;

    // ── Helper: spawn one star into an inactive slot ───────────────────
    // scattered=false → distribute z evenly across full depth range
    // scattered=true  → start at far end (z near kZFar) for burst effect
    void spawnStar (bool burst)
    {
        std::uniform_real_distribution<float> xyDist (-1.f, 1.f);
        std::uniform_real_distribution<float> zScattered (0.15f, kZFar);
        std::uniform_real_distribution<float> zBurst (0.75f, kZFar);
        std::uniform_real_distribution<float> sizeDist (0.8f, 2.8f);
        std::uniform_real_distribution<float> hueDist (-0.05f, 0.05f);

        for (auto& s : stars_)
        {
            if (s.active) continue;
            s.x          = xyDist (rng_);
            s.y          = xyDist (rng_);
            s.z          = burst ? zBurst (rng_) : zScattered (rng_);
            s.size       = sizeDist (rng_);
            s.hueOffset  = hueDist (rng_);
            s.active     = true;
            return;
        }
    }
};

// ─── DLL entry point ──────────────────────────────────────────────────────────
extern "C" __declspec(dllexport) IVisualizerPlugin* createVisualizer()
{
    return new StarfieldVisualizer();
}
