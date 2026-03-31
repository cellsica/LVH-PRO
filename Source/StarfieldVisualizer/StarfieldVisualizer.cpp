// StarfieldVisualizer.cpp — LVH Visualizer SDK Plugin
//
// Two visual layers:
//
//  [1] Ambient dots (silent-mode warp stars)
//      • 1px dots, no perspective size scaling, ~4/sec
//      • Warp-outward from centre (x/z, y/z projection)
//
//  [2] Band tiles (music-reactive, 16 EQ bands, circular layout)
//      • 16 bands arranged in a circle (evenly spaced angles)
//      • Each band tile spawns at its angle on a world-space ring
//      • Perspective projection makes tiles burst outward as z decreases
//      • Tile rectangle is oriented tangentially (wide side ⊥ radius)
//      • Trail: 4 faded copies drawn behind each tile
//      • Colour: blue (quiet) → red (loud)  — Doppler redshift

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
static constexpr int   kMaxDots          = 180;
static constexpr float kDotSpeed         = 0.0026f;
static constexpr int   kDotSpawnSilent   = 14;   // ~4/sec (silent)
static constexpr int   kDotSpawnLoudest  = 4;    // ~15/sec (loud)

// Band tiles — circular layout
static constexpr int   kMaxTiles       = kBands * 22;
static constexpr float kOrbitRadius    = 0.40f;  // world-space radius of tile ring
static constexpr float kTileWorldH     = 0.040f; // tile height in radial direction
static constexpr float kTileSpeedMin   = 0.018f;
static constexpr float kTileSpeedMax   = 0.095f;
static constexpr float kEmitThreshold  = 0.00008f;
static constexpr int   kTileSpawnLoud  = 8;      // min frames between spawns (loud)
static constexpr int   kTileSpawnQuiet = 22;     // max frames between spawns (at threshold)

// Shared
static constexpr float kZNear          = 0.05f;
static constexpr float kZFar           = 1.0f;
static constexpr int   kTrailSteps     = 4;
static constexpr float kTrailSpacing   = 5.0f;  // trail step in z-speed multiples

// ─── Particle structures ──────────────────────────────────────────────────────

struct Dot
{
    float x = 0.f, y = 0.f;
    float z = 1.f;
    bool  active = false;
};

struct Tile
{
    float x      = 0.f;   // world X = cos(angle) * kOrbitRadius
    float y      = 0.f;   // world Y = sin(angle) * kOrbitRadius
    float z      = 1.f;
    float speed  = 0.f;
    float energy = 0.f;   // 0..1, drives colour hue
    float angle  = 0.f;   // band direction angle (radians) — for tile rotation
    float worldW = 0.f;   // tangential width in world space
    float life   = 1.f;   // 1.0 at spawn → 0.0 at death (drives brightness/alpha)
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
        source_   = source;
        dotTimer_ = 0;

        std::fill (tileTimers_.begin(), tileTimers_.end(), 0);
        std::fill (bands_.begin(),      bands_.end(),      0.f);
        std::fill (smoothed_.begin(),   smoothed_.end(),   0.f);

        for (auto& d : dots_)  d.active = false;
        for (auto& t : tiles_) t.active = false;

        // Logarithmic band boundaries
        const float logMin = std::log2 (1.f);
        const float logMax = std::log2 (480.f);
        for (int b = 0; b <= kBands; ++b)
        {
            float t   = (float)b / (float)kBands;
            int   bin = (int)std::round (std::pow (2.f, logMin + t * (logMax - logMin)));
            bandBounds_[b] = juce::jlimit (1, kRawBins - 1, bin);
        }

        // Pre-seed ambient dots
        for (int i = 0; i < 60; ++i)
            spawnDot (true);
    }

    // ── Render ────────────────────────────────────────────────────────────

    void render (juce::Graphics& g, juce::OpenGLContext*) override
    {
        if (source_ == nullptr) return;

        const auto  bounds = g.getClipBounds().toFloat();
        const float cx     = bounds.getCentreX();
        const float cy     = bounds.getCentreY();
        const float scale  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.50f;

        // ── FFT → 16 log-spaced bands ─────────────────────────────────
        float raw[kRawBins] = {};
        source_->getFFTData (raw, kRawBins);

        for (int i = 0; i < kRawBins; ++i)
            smoothed_[i] = smoothed_[i] * 0.72f + raw[i] * 0.28f;

        for (int b = 0; b < kBands; ++b)
        {
            const int s = bandBounds_[b], e = bandBounds_[b + 1];
            if (e <= s) { bands_[b] *= 0.6f; continue; }
            float sum = 0.f;
            for (int i = s; i < e; ++i) sum += smoothed_[i];
            bands_[b] = bands_[b] * 0.6f + (sum / (float)(e - s)) * 0.4f;
        }

        // ── Background ────────────────────────────────────────────────
        g.fillAll (juce::Colour (0xff06060e));

        // ── Overall energy → dynamic dot spawn rate ───────────────────
        float overallEnergy = 0.f;
        for (int b = 0; b < kBands; ++b) overallEnergy += bands_[b];
        overallEnergy /= (float)kBands;
        // Normalised 0..1 (sqrt for perceptual scaling)
        const float overallNorm = juce::jmin (1.f, std::sqrt (overallEnergy / 0.0005f));
        // Interval: silent=14f → loud=4f
        const int dotInterval = juce::jmax (kDotSpawnLoudest,
            juce::roundToInt (kDotSpawnSilent * (1.f - overallNorm * 0.72f)));

        // ── Ambient dots (1px, no perspective size) ───────────────────
        if (++dotTimer_ >= dotInterval)
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

            const float progress = 1.f - (d.z / kZFar);
            g.setColour (juce::Colour (0xffffffff).withAlpha (0.15f + progress * 0.55f));
            g.fillRect (sx - 0.5f, sy - 0.5f, 1.f, 1.f);
        }

        // ── Tile arc width per band (tangential, world space) ─────────
        // Arc length per band at kOrbitRadius = 2π*R / kBands * fillFactor
        const float tileArcW = kOrbitRadius
                               * juce::MathConstants<float>::twoPi
                               / (float)kBands
                               * 0.75f;

        // ── Spawn new tiles per band ──────────────────────────────────
        for (int b = 0; b < kBands; ++b)
        {
            if (tileTimers_[b] > 0) { --tileTimers_[b]; continue; }
            if (bands_[b] < kEmitThreshold) continue;

            const float energyNorm = juce::jmin (1.f, std::sqrt (bands_[b] / 0.002f));
            spawnTile (b, energyNorm, tileArcW);

            tileTimers_[b] = juce::roundToInt (
                juce::jmax ((float)kTileSpawnLoud,
                            (float)kTileSpawnQuiet * (1.f - energyNorm)));
        }

        // ── Update + draw tiles ───────────────────────────────────────
        for (auto& t : tiles_)
        {
            if (!t.active) continue;
            t.z    -= t.speed;
            t.life -= t.speed / (kZFar - kZNear);  // reaches 0 when z reaches kZNear
            if (t.z < kZNear || t.life <= 0.f) { t.active = false; continue; }

            // テーマカラー連動: 低エネルギー = quietHue_, 高エネルギー = loudHue_
            const float hue  = quietHue_ + (loudHue_ - quietHue_) * t.energy;
            const float sat  = 0.75f + t.energy * 0.25f;
            // Attack → decay: bright at spawn (life=1), dims as tile travels (life→0)
            const float bri  = 0.25f + t.life * 0.75f;
            const float alph = 0.15f + t.life * 0.85f;
            const juce::Colour col = juce::Colour::fromHSV (hue, sat, bri,
                                                             juce::jmin (1.f, alph));

            // Tile orientation:
            //   tangential direction (wide axis) = (-sin(angle), cos(angle))
            //   radial direction    (thin axis)  = ( cos(angle), sin(angle))
            const float cosA  = std::cos (t.angle);
            const float sinA  = std::sin (t.angle);
            const float tangX = -sinA;
            const float tangY =  cosA;
            const float radX  =  cosA;
            const float radY  =  sinA;

            // Draw trail (farthest first, main tile last)
            for (int step = kTrailSteps; step >= 0; --step)
            {
                const float tz = t.z + step * t.speed * kTrailSpacing;
                if (tz > kZFar + 0.2f) continue;

                const float tsx = cx + (t.x / tz) * scale;
                const float tsy = cy + (t.y / tz) * scale;
                const float tw  = t.worldW   / tz * scale;   // tangential width
                const float th  = kTileWorldH / tz * scale;  // radial height

                // Cull off-screen (conservative bounding radius)
                const float diagR = std::sqrt (tw * tw + th * th) * 0.5f;
                if (tsx + diagR < 0.f || tsx - diagR > bounds.getWidth() ||
                    tsy + diagR < 0.f || tsy - diagR > bounds.getHeight())
                    continue;

                const float trailAlpha = (step == 0) ? 1.f
                                                      : (1.f - step * 0.22f);
                const float drawAlpha  = col.getFloatAlpha() * trailAlpha;

                // Build rotated quad corners:
                //   ±tw/2 in tangential direction
                //   ±th/2 in radial direction
                const float hw = tw * 0.5f, hh = th * 0.5f;
                juce::Path quad;
                quad.startNewSubPath (tsx + (-hw) * tangX + (-hh) * radX,
                                      tsy + (-hw) * tangY + (-hh) * radY);
                quad.lineTo          (tsx + ( hw) * tangX + (-hh) * radX,
                                      tsy + ( hw) * tangY + (-hh) * radY);
                quad.lineTo          (tsx + ( hw) * tangX + ( hh) * radX,
                                      tsy + ( hw) * tangY + ( hh) * radY);
                quad.lineTo          (tsx + (-hw) * tangX + ( hh) * radX,
                                      tsy + (-hw) * tangY + ( hh) * radY);
                quad.closeSubPath();

                g.setColour (col.withAlpha (drawAlpha));
                g.fillPath  (quad);

                // Leading-edge highlight (main tile only)
                if (step == 0 && tw > 5.f)
                {
                    // Thin bright strip on the outer (radial +) edge
                    const float hs = juce::jmin (hh * 0.25f, 2.5f);
                    juce::Path highlight;
                    highlight.startNewSubPath (tsx + (-hw) * tangX + (hh - hs) * radX,
                                               tsy + (-hw) * tangY + (hh - hs) * radY);
                    highlight.lineTo          (tsx + ( hw) * tangX + (hh - hs) * radX,
                                               tsy + ( hw) * tangY + (hh - hs) * radY);
                    highlight.lineTo          (tsx + ( hw) * tangX + (hh      ) * radX,
                                               tsy + ( hw) * tangY + (hh      ) * radY);
                    highlight.lineTo          (tsx + (-hw) * tangX + (hh      ) * radX,
                                               tsy + (-hw) * tangY + (hh      ) * radY);
                    highlight.closeSubPath();
                    g.setColour (juce::Colours::white.withAlpha (drawAlpha * 0.45f));
                    g.fillPath  (highlight);
                }
            }
        }
    }

    void shutdown() override { source_ = nullptr; }

    void setThemeColors (uint32_t mainColor, uint32_t accentColor) override
    {
        loudHue_  = juce::Colour (mainColor).getHue();    // e.g. 0.0 (red) or 0.5 (cyan)
        quietHue_ = juce::Colour (accentColor).getHue();  // e.g. 0.08 (amber) or 0.08 (orange)
    }

    int getPreferredWidth()  const override { return 520; }
    int getPreferredHeight() const override { return 520; }

private:
    // ── State ─────────────────────────────────────────────────────────
    IAudioSource* source_ = nullptr;

    std::array<Dot,  kMaxDots>   dots_  {};
    std::array<Tile, kMaxTiles>  tiles_ {};

    std::array<float, kRawBins>   smoothed_   {};
    std::array<float, kBands>     bands_      {};
    std::array<int,   kBands + 1> bandBounds_ {};
    std::array<int,   kBands>     tileTimers_ {};

    std::mt19937 rng_;
    int          dotTimer_ = 0;

    // Theme colors (Phase E) — hue values in [0, 1]
    // Defaults = original Doppler redshift: quiet=blue(0.65), loud=red(0.0)
    float quietHue_ = 0.65f;  // low energy color  (Warm=amber 0.08, Neon=orange 0.08)
    float loudHue_  = 0.0f;   // high energy color  (Warm=red 0.0,   Neon=cyan 0.5)

    // ── Spawn helpers ─────────────────────────────────────────────────

    void spawnDot (bool scatter)
    {
        std::uniform_real_distribution<float> xyDist  (-0.08f, 0.08f);
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

    void spawnTile (int band, float energyNorm, float tileArcW)
    {
        const float angle = (float)band / (float)kBands
                            * juce::MathConstants<float>::twoPi;

        const float speed = kTileSpeedMin
                            + energyNorm * (kTileSpeedMax - kTileSpeedMin);

        for (auto& t : tiles_)
        {
            if (t.active) continue;
            t.x      = std::cos (angle) * kOrbitRadius;
            t.y      = std::sin (angle) * kOrbitRadius;
            t.z      = kZFar;
            t.speed  = speed;
            t.energy = energyNorm;
            t.angle  = angle;
            t.worldW = tileArcW;
            t.life   = 1.0f;
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
