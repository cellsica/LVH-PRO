// StarfieldVisualizer.cpp — LVH Visualizer SDK Plugin (Redesigned)
//
// Two visual layers:
//
//  [1] Ambient dots (always visible, silent-mode warp stars)
//      • 1px dots, no perspective size scaling
//      • Very slow speed, ~2-5 spawns per second
//      • Fly outward from centre (warp projection: x/z, y/z)
//
//  [2] Band tiles (music-reactive, 16 EQ bands)
//      • 16 horizontal lanes across X axis
//      • Each band spawns rectangular "LED segment" tiles from far-z
//      • Tiles grow with perspective as they fly toward camera
//      • Trail: 4 faded copies drawn behind each tile
//      • Color: blue (quiet) → red (loud)  — Doppler redshift

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../VisualizerSDK/IVisualizerPlugin.h"
#include "../VisualizerSDK/IAudioSource.h"
#include <array>
#include <cmath>
#include <random>

// ─── Constants ────────────────────────────────────────────────────────────────

static constexpr int   kRawBins        = 512;
static constexpr int   kBands          = 16;

// Ambient dots
static constexpr int   kMaxDots        = 120;
static constexpr float kDotSpeed       = 0.0026f;   // z-decrement per frame
static constexpr int   kDotSpawnFrames = 14;        // frames between dot spawns (~4/sec)

// Band tiles
static constexpr int   kMaxTiles       = kBands * 22; // pool per band × lanes
static constexpr float kTileSpeedMin   = 0.018f;
static constexpr float kTileSpeedMax   = 0.095f;
static constexpr float kTileWorldH     = 0.045f;    // fixed world-space height
static constexpr float kEmitThreshold  = 0.00008f;  // same floor as RadialVisualizer
static constexpr int   kTileSpawnLoud  = 3;         // min frames between spawns (loud)
static constexpr int   kTileSpawnQuiet = 10;        // max frames between spawns (threshold)

// Shared
static constexpr float kZNear          = 0.05f;
static constexpr float kZFar           = 1.0f;
static constexpr int   kTrailSteps     = 4;
static constexpr float kTrailSpacing   = 5.0f;      // trail step in z-speed multiples

// ─── Particle structures ──────────────────────────────────────────────────────

struct Dot
{
    float x = 0.f, y = 0.f;   // world space, small near-centre values
    float z = 1.f;
    bool  active = false;
};

struct Tile
{
    float x      = 0.f;   // world X — band lane centre
    float y      = 0.f;   // world Y — slight random offset
    float z      = 1.f;
    float speed  = 0.f;
    float energy = 0.f;   // 0..1, drives colour hue and alpha
    bool  active = false;
};

// ─── Visualizer ───────────────────────────────────────────────────────────────

class StarfieldVisualizer : public IVisualizerPlugin
{
public:
    StarfieldVisualizer() : rng_ (std::random_device{}()) {}

    // ── Lifecycle ─────────────────────────────────────────────────────────

    void initialise (IAudioSource* source) override
    {
        source_    = source;
        dotTimer_  = 0;

        std::fill (tileTimers_.begin(), tileTimers_.end(), 0);
        std::fill (bands_.begin(),      bands_.end(),      0.f);
        std::fill (smoothed_.begin(),   smoothed_.end(),   0.f);

        for (auto& d : dots_)  d.active = false;
        for (auto& t : tiles_) t.active = false;

        // Logarithmic band boundaries (bin 1 ≈ 43 Hz to bin 480 ≈ 20.6 kHz)
        const float logMin = std::log2 (1.f);
        const float logMax = std::log2 (480.f);
        for (int b = 0; b <= kBands; ++b)
        {
            float t   = (float)b / (float)kBands;
            int   bin = (int)std::round (std::pow (2.f, logMin + t * (logMax - logMin)));
            bandBounds_[b] = juce::jlimit (1, kRawBins - 1, bin);
        }

        // Pre-seed dots spread across depth range
        for (int i = 0; i < 60; ++i)
            spawnDot (true /*scatter*/);
    }

    // ── Render ────────────────────────────────────────────────────────────

    void render (juce::Graphics& g, juce::OpenGLContext*) override
    {
        if (source_ == nullptr) return;

        const auto  bounds = g.getClipBounds().toFloat();
        const float cx     = bounds.getCentreX();
        const float cy     = bounds.getCentreY();
        const float scale  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.50f;

        // ── FFT → 16 bands ────────────────────────────────────────────
        float raw[kRawBins] = {};
        source_->getFFTData (raw, kRawBins);

        for (int i = 0; i < kRawBins; ++i)
            smoothed_[i] = smoothed_[i] * 0.72f + raw[i] * 0.28f;

        for (int b = 0; b < kBands; ++b)
        {
            const int s = bandBounds_[b], e = bandBounds_[b + 1];
            if (e <= s) { bands_[b] = bands_[b] * 0.6f; continue; }
            float sum = 0.f;
            for (int i = s; i < e; ++i) sum += smoothed_[i];
            bands_[b] = bands_[b] * 0.6f + (sum / (float)(e - s)) * 0.4f;
        }

        // ── Background ────────────────────────────────────────────────
        g.fillAll (juce::Colour (0xff06060e));

        // ── Ambient dots ──────────────────────────────────────────────
        // Spawn ~4/sec
        if (++dotTimer_ >= kDotSpawnFrames)
        {
            dotTimer_ = 0;
            spawnDot (false);
        }

        for (auto& d : dots_)
        {
            if (!d.active) continue;
            d.z -= kDotSpeed;
            if (d.z < kZNear) { d.active = false; continue; }

            const float sx = cx + (d.x / d.z) * scale;
            const float sy = cy + (d.y / d.z) * scale;
            if (sx < 0.f || sx > bounds.getWidth() ||
                sy < 0.f || sy > bounds.getHeight())
            { d.active = false; continue; }

            // 1px dot — no perspective sizing
            const float progress = 1.f - (d.z / kZFar);
            const float alpha    = 0.15f + progress * 0.55f;
            g.setColour (juce::Colour (0xffffffff).withAlpha (alpha));
            g.fillRect (sx - 0.5f, sy - 0.5f, 1.f, 1.f);
        }

        // ── Band tiles ────────────────────────────────────────────────
        // Lane layout: 16 bands evenly across X = -0.85 .. +0.85
        const float laneW       = 1.70f / (float)kBands;
        const float tileWorldW  = laneW * 0.80f;

        // Spawn new tiles per band
        for (int b = 0; b < kBands; ++b)
        {
            if (tileTimers_[b] > 0) { --tileTimers_[b]; continue; }

            if (bands_[b] < kEmitThreshold) continue;

            // Normalised energy 0..1 (sqrt for perceptual scaling)
            const float energyNorm = juce::jmin (1.f, std::sqrt (bands_[b] / 0.002f));

            spawnTile (b, energyNorm, laneW);

            // Next spawn delay: loud → fast, quiet → slow
            tileTimers_[b] = juce::roundToInt (
                juce::jmax ((float)kTileSpawnLoud,
                            (float)kTileSpawnQuiet * (1.f - energyNorm)));
        }

        // Update + draw tiles (trail first, then main tile)
        for (auto& t : tiles_)
        {
            if (!t.active) continue;
            t.z -= t.speed;
            if (t.z < kZNear) { t.active = false; continue; }

            const float progress = 1.f - (t.z / kZFar);

            // Doppler colour: energy=0→blue (hue 0.65), energy=1→red (hue 0.0)
            const float hue  = 0.65f * (1.f - t.energy);
            const float sat  = 0.75f + t.energy * 0.25f;
            const float bri  = 0.55f + progress * 0.45f;
            const float alph = 0.25f + progress * 0.70f;
            const juce::Colour col = juce::Colour::fromHSV (hue, sat, bri,
                                                             juce::jmin (1.f, alph));

            // Draw trail (back to front so main tile is on top)
            for (int step = kTrailSteps; step >= 0; --step)
            {
                const float tz = t.z + step * t.speed * kTrailSpacing;
                if (tz > kZFar + 0.1f) continue;

                const float tsx = cx + (t.x / tz) * scale;
                const float tsy = cy + (t.y / tz) * scale;
                const float tw  = tileWorldW / tz * scale;
                const float th  = kTileWorldH / tz * scale;

                if (tsx + tw * 0.5f < 0.f || tsx - tw * 0.5f > bounds.getWidth() ||
                    tsy + th * 0.5f < 0.f || tsy - th * 0.5f > bounds.getHeight())
                    continue;

                const float trailAlpha = (step == 0) ? 1.f
                                                      : (1.f - step * 0.22f);
                g.setColour (col.withAlpha (col.getFloatAlpha() * trailAlpha));

                // Rounded rect for main tile, plain rect for trail
                if (step == 0)
                    g.fillRoundedRectangle (tsx - tw * 0.5f, tsy - th * 0.5f,
                                            tw, th, th * 0.25f);
                else
                    g.fillRect (tsx - tw * 0.5f, tsy - th * 0.5f, tw, th);

                // Bright highlight on leading edge of main tile
                if (step == 0 && tw > 4.f)
                {
                    g.setColour (juce::Colours::white.withAlpha (
                        col.getFloatAlpha() * 0.35f));
                    g.fillRoundedRectangle (tsx - tw * 0.5f, tsy - th * 0.5f,
                                            tw, th * 0.22f, th * 0.22f);
                }
            }
        }
    }

    void shutdown() override { source_ = nullptr; }

    int getPreferredWidth()  const override { return 520; }
    int getPreferredHeight() const override { return 520; }

private:
    // ── State ─────────────────────────────────────────────────────────
    IAudioSource* source_ = nullptr;

    std::array<Dot,  kMaxDots>   dots_  {};
    std::array<Tile, kMaxTiles>  tiles_ {};

    std::array<float, kRawBins>   smoothed_    {};
    std::array<float, kBands>     bands_       {};
    std::array<int,   kBands + 1> bandBounds_  {};
    std::array<int,   kBands>     tileTimers_  {};

    std::mt19937 rng_;
    int          dotTimer_ = 0;

    // ── Spawn helpers ─────────────────────────────────────────────────

    void spawnDot (bool scatter)
    {
        std::uniform_real_distribution<float> xyDist (-0.08f, 0.08f);   // near centre
        std::uniform_real_distribution<float> zScatter (0.12f, kZFar);

        for (auto& d : dots_)
        {
            if (d.active) continue;
            d.x      = xyDist (rng_);
            d.y      = xyDist (rng_);
            d.z      = scatter ? zScatter (rng_) : kZFar;
            d.active = true;
            return;
        }
    }

    void spawnTile (int band, float energyNorm, float laneW)
    {
        const float bandCentreX = -0.85f + (band + 0.5f) * laneW;

        std::uniform_real_distribution<float> yJitter (-0.03f, 0.03f);

        const float tileSpeed = kTileSpeedMin
                                + energyNorm * (kTileSpeedMax - kTileSpeedMin);

        for (auto& t : tiles_)
        {
            if (t.active) continue;
            t.x      = bandCentreX;
            t.y      = yJitter (rng_);
            t.z      = kZFar;
            t.speed  = tileSpeed;
            t.energy = energyNorm;
            t.active = true;
            return;
        }
    }
};

// ─── DLL entry point ──────────────────────────────────────────────────────────
extern "C" __declspec(dllexport) IVisualizerPlugin* createVisualizer()
{
    return new StarfieldVisualizer();
}
