// =============================================================================
// LVH RadialVisualizer v3
//
// FFT 512 ビンを対数スケール 16 バンドに集約（グライコ風）。
// 低域が詰まって見えなくなる問題を解消。
//
// Visual layers (back to front):
//   1. Background radial gradient
//   2. Wave rings — 16-band 波形が外側へ膨張、ドップラー配色
//   3. Radial bars — 16 バンドを滑らかに補間して 512 本分描画
//   4. Nucleus — glowing core + orbiting particles
// =============================================================================

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_opengl/juce_opengl.h>
#include <array>
#include <deque>
#include <cmath>
#include "../VisualizerSDK/IVisualizerPlugin.h"
#include "../VisualizerSDK/IAudioSource.h"

class RadialVisualizer final : public IVisualizerPlugin
{
public:
    static constexpr int   kRawBins      = 512;
    static constexpr int   kBands        = 16;    // 対数スケール 16 バンド
    static constexpr float kEmitSpeed    = 2.0f;
    static constexpr int   kEmitInterval = 3;
    static constexpr int   kMaxRings     = 45;
    static constexpr float kEmitThreshold = 0.005f; // バンド平均がこれ以下なら無音

    // ── Wave ring ─────────────────────────────────────────────────────────
    struct WaveRing
    {
        std::array<float, kBands> band {};   // 16 バンドのスナップショット
        float baseRadius = 0.f;
    };

    // ── Orbiting particle ─────────────────────────────────────────────────
    struct Particle
    {
        float orbitRadius, speed, phase, dotSize;
    };

    // ── Lifecycle ─────────────────────────────────────────────────────────

    void initialise (IAudioSource* source) override
    {
        source_ = source;
        std::fill (smoothed_.begin(), smoothed_.end(), 0.f);
        std::fill (bands_.begin(),    bands_.end(),    0.f);
        frameCount_ = 0;
        rings_.clear();

        // 対数スケールでビン境界を計算（bin 1 ≈ 43 Hz 〜 bin 480 ≈ 20.6 kHz）
        const float logMin = std::log2 (1.f);
        const float logMax = std::log2 (480.f);
        for (int b = 0; b <= kBands; ++b)
        {
            float t = (float)b / (float)kBands;
            int   bin = (int)std::round (std::pow (2.f, logMin + t * (logMax - logMin)));
            bandBounds_[b] = juce::jlimit (1, kRawBins - 1, bin);
        }

        particles_[0] = {  9.f,  0.047f, 0.000f, 2.8f };
        particles_[1] = { 14.f, -0.031f, 2.094f, 2.2f };
        particles_[2] = {  6.f,  0.073f, 4.189f, 2.0f };
        particles_[3] = { 12.f, -0.055f, 1.047f, 1.8f };
        particles_[4] = {  7.f,  0.089f, 3.665f, 1.5f };
    }

    // ── Render ────────────────────────────────────────────────────────────

    void render (juce::Graphics& g, juce::OpenGLContext*) override
    {
        if (source_ == nullptr) return;

        // FFT 取得 + IIR スムージング
        float raw[kRawBins] = {};
        source_->getFFTData (raw, kRawBins);
        for (int i = 0; i < kRawBins; ++i)
            smoothed_[i] = smoothed_[i] * 0.72f + raw[i] * 0.28f;

        // 16 バンドへ集約（各バンド内の平均値）
        // ※ 低域バンドは bin 数が 1 以下になる場合があるためガード必須
        for (int b = 0; b < kBands; ++b)
        {
            int s     = bandBounds_[b];
            int e     = bandBounds_[b + 1];
            int count = e - s;
            if (count <= 0) { continue; }   // NaN 防止：スキップして前フレーム値を保持
            float sum = 0.f;
            for (int i = s; i < e; ++i) sum += smoothed_[i];
            bands_[b] = bands_[b] * 0.6f + (sum / (float)count) * 0.4f;
        }

        const auto  bounds = g.getClipBounds().toFloat();
        const float cx     = bounds.getCentreX();
        const float cy     = bounds.getCentreY();
        const float maxR   = std::min (bounds.getWidth(), bounds.getHeight()) * 0.48f;
        const float scaleY = maxR * 2.8f;

        // 低中域エネルギーで核をパルス（バンド 0〜4）
        float energy = 0.f;
        for (int b = 0; b < 5; ++b) energy += bands_[b];
        energy = juce::jmin (energy * 8.f, 1.f);

        const float coreR = 14.f + energy * 7.f;

        // ── 1. Background ─────────────────────────────────────────────
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff0c0c1e), cx, cy,
            juce::Colour (0xff020207), cx + maxR, cy + maxR,
            true));
        g.fillRect (bounds);

        // ── 2. Wave rings ─────────────────────────────────────────────
        ++frameCount_;

        // 全バンド平均でしきい値判定
        float totalEnergy = 0.f;
        for (int b = 0; b < kBands; ++b) totalEnergy += bands_[b];
        const float avgEnergy = totalEnergy / (float)kBands;

        if (frameCount_ % kEmitInterval == 0
            && avgEnergy > kEmitThreshold
            && (int)rings_.size() < kMaxRings)
        {
            WaveRing ring;
            ring.band       = bands_;
            ring.baseRadius = coreR + 2.f;
            rings_.push_back (ring);
        }

        for (auto it = rings_.begin(); it != rings_.end(); )
        {
            it->baseRadius += kEmitSpeed;
            const float progress = it->baseRadius / maxR;
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
    // ── バンド値を角度から補間取得 ─────────────────────────────────────────
    // 0 〜 1 の正規化角度 t に対して 16 バンド値を滑らかに補間する。
    float bandAtAngle (const std::array<float, kBands>& band, float t) const
    {
        // t を 0..kBands にマッピング
        float pos   = t * (float)kBands;
        int   lo    = (int)pos % kBands;
        int   hi    = (lo + 1) % kBands;
        float frac  = pos - (float)(int)pos;
        // コサイン補間でなめらかに
        float mu    = (1.f - std::cos (frac * juce::MathConstants<float>::pi)) * 0.5f;
        return band[lo] * (1.f - mu) + band[hi] * mu;
    }

    // ── Wave ring ─────────────────────────────────────────────────────────
    void drawWaveRing (juce::Graphics& g,
                       float cx, float cy,
                       const WaveRing& ring,
                       float scaleY, float alpha, float progress)
    {
        constexpr int kSteps = 256;   // リング描画の分割数
        juce::Path path;

        for (int i = 0; i <= kSteps; ++i)
        {
            const float t     = (float)(i % kSteps) / (float)kSteps;
            const float angle = t * juce::MathConstants<float>::twoPi
                                - juce::MathConstants<float>::halfPi;

            const float mag   = bandAtAngle (ring.band, t);
            const float r     = ring.baseRadius + mag * scaleY;
            const float px    = cx + r * std::cos (angle);
            const float py    = cy + r * std::sin (angle);

            if (i == 0) path.startNewSubPath (px, py);
            else        path.lineTo (px, py);
        }
        path.closeSubPath();

        // ドップラー配色：新しい（中心寄り）= 赤、古い（外側）= 青紫
        const float hue = progress * 0.72f;
        const float sat = 0.85f + (1.f - progress) * 0.10f;
        g.setColour (juce::Colour::fromHSV (hue, sat, 0.95f, alpha));
        g.strokePath (path, juce::PathStrokeType (1.3f));
    }

    // ── Radial bars ───────────────────────────────────────────────────────
    // 512 本の角度を 16 バンドに対応させ、バンド間をコサイン補間して描画。
    void drawRadialBars (juce::Graphics& g,
                         float cx, float cy,
                         float innerR, float maxR, float scaleY)
    {
        constexpr int kDrawBins = 512;

        for (int i = 0; i < kDrawBins; ++i)
        {
            const float t      = (float)i / (float)kDrawBins;
            const float mag    = bandAtAngle (bands_, t);
            const float barLen = juce::jmin (mag * scaleY, maxR - innerR);
            if (barLen < 0.4f) continue;

            const float angle = t * juce::MathConstants<float>::twoPi
                                - juce::MathConstants<float>::halfPi;
            const float cosA  = std::cos (angle);
            const float sinA  = std::sin (angle);

            // 大きい音量 = 赤 / 小さい音量 = シアン
            const float th  = juce::jmin (mag * 14.f, 1.f);
            const float hue = (1.f - th) * 0.50f;
            g.setColour (juce::Colour::fromHSV (hue, 0.9f, 1.0f, 0.80f + th * 0.15f));

            g.drawLine (cx + innerR * cosA, cy + innerR * sinA,
                        cx + (innerR + barLen) * cosA,
                        cy + (innerR + barLen) * sinA,
                        1.8f);
        }
    }

    // ── Nucleus ───────────────────────────────────────────────────────────
    void drawNucleus (juce::Graphics& g,
                      float cx, float cy,
                      float coreR, float energy)
    {
        // グロー
        for (int layer = 5; layer >= 1; --layer)
        {
            const float r  = coreR + (float)layer * 5.5f;
            const float al = 0.12f / (float)layer;
            g.setColour (juce::Colour::fromHSV (0.58f, 0.75f, 1.0f, al));
            g.fillEllipse (cx - r, cy - r, r * 2.f, r * 2.f);
        }

        // コア
        g.setColour (juce::Colour::fromHSV (0.60f, 0.55f, 0.95f, 0.75f));
        g.fillEllipse (cx - coreR, cy - coreR, coreR * 2.f, coreR * 2.f);

        // ハイライト
        const float cr = coreR * 0.38f;
        g.setColour (juce::Colours::white.withAlpha (0.88f));
        g.fillEllipse (cx - cr, cy - cr, cr * 2.f, cr * 2.f);

        // 軌道粒子
        for (auto& p : particles_)
        {
            p.phase += p.speed * (1.f + energy * 0.45f);

            const float pr = p.orbitRadius * (1.f + energy * 0.25f);
            const float px = cx + pr * std::cos (p.phase);
            const float py = cy + pr * std::sin (p.phase);

            const float gr = p.dotSize * 2.8f;
            g.setColour (juce::Colour::fromHSV (0.56f, 0.85f, 1.0f, 0.30f));
            g.fillEllipse (px - gr, py - gr, gr * 2.f, gr * 2.f);

            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.fillEllipse (px - p.dotSize * 0.65f, py - p.dotSize * 0.65f,
                           p.dotSize * 1.3f, p.dotSize * 1.3f);
        }
    }

    // ── State ─────────────────────────────────────────────────────────────
    IAudioSource*              source_      = nullptr;
    std::array<float, kRawBins> smoothed_  {};
    std::array<float, kBands>   bands_     {};
    int                         bandBounds_[kBands + 1] {};
    std::deque<WaveRing>        rings_;
    std::array<Particle, 5>     particles_  {};
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
