#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <cmath>

// =========================================================================
// VUPhysicsEngine
// Simulates the mechanical ballistics of a physical VU meter needle using
// a spring-mass-damper (2nd order ODE) stepped at 60 Hz.
//
// Physics model (per channel):
//   F_spring = spring * (target - angle)
//   F_damper = damping * velocity
//   accel    = (F_spring - F_damper) / mass
//   velocity += accel * dt
//   angle    += velocity * dt
//
// Parameters are tuned to match the IEC 60268-17 VU meter spec:
//   - 99% deflection in ~300 ms for a step input at 0 VU
//   - ~1–2% overshoot (slightly underdamped: ζ ≈ 0.57)
// =========================================================================
class VUPhysicsEngine : private juce::Timer
{
public:
    // dB range mapped to needle angle [0, 1]
    static constexpr float kMinDb   = -40.0f;   // angle = 0.0 (full left)
    static constexpr float kMaxDb   =   3.0f;   // angle = 1.0 (full right)
    static constexpr int   kTimerHz =  60;

    // Callback that returns the latest RMS for channel ch (0 = L, 1 = R).
    // Expected to call AudioEngine::exchangeRms(ch) — message thread safe.
    using RmsProvider = std::function<float(int ch)>;

    explicit VUPhysicsEngine (RmsProvider rmsCallback)
        : getRms_ (std::move (rmsCallback))
    {}

    void start() { startTimerHz (kTimerHz); }
    void stop()  { stopTimer(); }

    // Returns needle angle in [0, ~1.1].
    //   0.0 = full left (-20 VU)
    //   1.0 = 0 VU reference mark
    //   > 1.0 = overshoot into the red zone (+3 VU = ~1.13)
    // Read from the message thread (same thread as timerCallback).
    float getNeedleAngle (int ch) const noexcept
    {
        jassert (ch == 0 || ch == 1);
        return angle_[ch & 1];
    }

    // Physical parameters — can be tweaked for "feel".
    float mass    = 1.0f;
    float spring  = 196.0f;   // ω_n = sqrt(spring/mass) ≈ 14.0 rad/s
    float damping =  16.0f;   // ζ  = damping / (2 * sqrt(spring*mass)) ≈ 0.57

private:
    void timerCallback() override
    {
        constexpr float dt = 1.0f / (float) kTimerHz;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float rms    = getRms_ ? getRms_ (ch) : 0.f;
            const float target = rmsToAngle (rms);

            const float springForce = spring  * (target - angle_[ch]);
            const float damperForce = damping * vel_[ch];
            const float accel       = (springForce - damperForce) / mass;

            vel_[ch]   += accel * dt;
            angle_[ch] += vel_[ch] * dt;

            // Hard stop at left bumper — no negative angle
            if (angle_[ch] < 0.f)
            {
                angle_[ch] = 0.f;
                vel_[ch]   = 0.f;
            }
        }
    }

    // Converts a linear RMS value to a needle angle in [0, 1+].
    // Linear mapping in dB space: kMinDb → 0.0, kMaxDb → 1.0.
    static float rmsToAngle (float rms) noexcept
    {
        if (rms <= 0.f) return 0.f;
        const float db = 20.0f * std::log10 (rms);
        if (db <= kMinDb) return 0.f;
        return (db - kMinDb) / (kMaxDb - kMinDb);
    }

    RmsProvider getRms_;
    float angle_[2] = {};   // current needle angle — message thread only
    float vel_[2]   = {};   // current needle velocity — message thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VUPhysicsEngine)
};
