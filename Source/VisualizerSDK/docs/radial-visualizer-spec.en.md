# RadialVisualizer Plugin Specification

**Plugin:** RadialVisualizer  
**Version:** 1.0  
**Audience:** Third-party developers (including vibe coders using AI assistants)

---

## 1. Overview

RadialVisualizer renders audio frequency energy as a circular visualization.

Two primary visual elements react to audio in concert: **wave rings** that expand outward from the center, and **radial bars** arranged around the circumference. During quiet passages, delicate pulses appear; during dynamic audio, powerful rings radiate outward. The color scheme mimics a Doppler effect — shifting from red at the center to violet at the outer edge — giving a visual sense of "sonic distance."

**Preferred canvas size:** 520 × 520 px

---

## 2. Rendering Layer Structure

The visualization is composited from four layers drawn in order:

| Layer Order | Layer Name | Contents |
|:---:|:---|:---|
| 1 | Background | Solid black fill |
| 2 | Wave Rings | Concentric expanding rings driven by audio energy |
| 3 | Radial Bars | 512 frequency bars radiating from center |
| 4 | Nucleus | Glowing core circle with 5 orbiting particles |

Each layer is drawn independently, with upper layers composited over lower ones.

---

## 3. FFT Data Processing

### 3.1 Input

- FFT size: **512 bins** (`kRawBins = 512`)
- Used frequency range: bins 1 through 480 (DC component and near-Nyquist bins excluded)

### 3.2 Aggregation into 16 Bands

The 512 bins are aggregated into **16 logarithmically-spaced bands**.

```
bandBounds: log2(1) to log2(480) divided into 16 equal segments
```

The amplitude average of the bins within each band's range is computed and stored as `band[0..15]`.

```
band[b] = average( fftMagnitude[binStart..binEnd] )
```

The logarithmic scale assigns finer resolution to low frequencies (bass) and coarser resolution to high frequencies (treble), closely matching human auditory sensitivity.

### 3.3 Overall Energy Calculation

The average energy across all bands is used for ring emission decisions:

```
avgEnergy = average( band[0..15] )
```

---

## 4. Wave Ring Specification

### 4.1 WaveRing Structure

```cpp
struct WaveRing {
    std::array<float, kBands> band {};  // Frequency snapshot at emission time
    float baseRadius  = 0.f;            // Base radius at time of emission
    float emitEnergy  = 0.f;            // Normalized energy at emission (0–1)
};
```

### 4.2 Ring Emission Logic

1. **Frame interval check:** Emission is attempted every `kEmitInterval` (3) frames.
2. **Energy threshold check:** A ring is emitted only if `avgEnergy > kEmitThreshold` (0.00008).
3. **Energy normalization:**
   ```
   ring.emitEnergy = min( sqrt(avgEnergy / 0.05), 1.0 )
   ```
4. **Band snapshot:** The current `band[0..15]` values are stored in the ring at emission time.
5. **Ring count limit:** Maximum `kMaxRings` (45) rings active simultaneously. When the limit is reached, the oldest ring is removed (FIFO).

### 4.3 Ring Expansion

Each frame, a ring's current radius is computed as:

```
currentRadius = baseRadius + framesAlive * kEmitSpeed
```

A ring is automatically retired when its radius exceeds the canvas diagonal.

### 4.4 Stroke Width (Attack-to-Decay Feel)

Rings are drawn thick immediately after emission and become thinner as they expand, conveying an "attack → decay" sensation.

```
strokeThick = 1.8 + ring.emitEnergy * 2.2   // at emission (progress=0)
strokeThin  = 0.5 + ring.emitEnergy * 0.5   // when fully expanded (progress=1)
strokeW     = strokeThin + (1 - progress) * (strokeThick - strokeThin)
```

`progress` = `currentRadius / maxRadius` (range 0–1)

### 4.5 Constants Reference

| Constant | Value | Description | Effect of Changing |
|:---|:---:|:---|:---|
| `kEmitSpeed` | 2.0 | Ring expansion speed per frame (px) | Higher = rings vanish faster; lower = rings linger longer |
| `kEmitInterval` | 3 | Frames between ring emission attempts | Lower = more frequent rings; higher = sparser rings |
| `kMaxRings` | 45 | Maximum simultaneous active rings | Higher = more afterimage rings; lower = cleaner look |
| `kEmitThreshold` | 0.00008 | Minimum energy required to emit a ring | Higher = rings suppressed during quiet passages |

---

## 5. Radial Bar Rendering

### 5.1 Bar Count and Angles

512 radial bars are distributed evenly around the circle:

```
angle[i] = i * 2π / 512    (i = 0..511)
```

### 5.2 Cosine Interpolation for Smooth Band Values

The 16 band values are smoothly interpolated across all 512 bars using **cosine interpolation**:

```
float bandAtAngle(band[], t):
    pos  = t * kBands                     // continuous position (0–16)
    lo   = floor(pos) % kBands            // lower band index
    hi   = (lo + 1) % kBands             // upper band index
    frac = pos - floor(pos)              // fractional part
    mu   = (1 - cos(frac * π)) * 0.5    // cosine interpolation weight
    return band[lo] * (1 - mu) + band[hi] * mu
```

`t` = `i / 512` (normalized bar index). Cosine interpolation produces smoother transitions than linear interpolation.

### 5.3 Bar Drawing

Each bar is drawn from the canvas center outward:

```
startX = cx + cos(angle[i]) * innerRadius
startY = cy + sin(angle[i]) * innerRadius
endX   = cx + cos(angle[i]) * (innerRadius + barHeight)
endY   = cy + sin(angle[i]) * (innerRadius + barHeight)
```

`barHeight` is scaled proportionally to the `bandAtAngle` return value.

### 5.4 Doppler Coloring

Each bar's color is determined by the `progress` value of the nearest active wave ring:

```
hue = 0.0 + progress * 0.72   // 0.0 (red) → 0.72 (violet)
```

If no wave rings are active, a default color (white or dark blue) is used.

---

## 6. Nucleus

### 6.1 Core Circle (Glow)

A glowing circle is drawn at the canvas center.

- A radial gradient is used, transitioning from transparent at the outer edge to opaque at the center.
- The glow radius varies subtly with overall audio energy.

### 6.2 Orbiting Particles

Five particles orbit the core in continuous motion.

```cpp
struct Particle {
    float orbitRadius;  // Orbit radius (px)
    float speed;        // Angular velocity (rad/frame)
    float phase;        // Initial phase offset (rad)
    float dotSize;      // Dot diameter (px)
};
```

Each particle has distinct `orbitRadius`, `speed`, and `phase` values, producing organic, non-synchronized motion. Per-frame position:

```
x = cx + cos(phase + speed * frame) * orbitRadius
y = cy + sin(phase + speed * frame) * orbitRadius
```

---

## 7. Tuning Guide

Modifying the following constants significantly changes the visual character of the plugin.

| Constant | Value | Description | Effect of Changing |
|:---|:---:|:---|:---|
| `kRawBins` | 512 | FFT bin count | Changing this requires revisiting band aggregation logic |
| `kBands` | 16 | Number of frequency bands | Higher = finer frequency resolution; bar interpolation logic must also be updated |
| `kEmitSpeed` | 2.0 | Ring expansion speed (px/frame) | Higher = faster-disappearing rings; lower = long-lingering rings |
| `kEmitInterval` | 3 | Ring emission interval (frames) | Lower = denser rings; higher = sparser rings |
| `kMaxRings` | 45 | Maximum simultaneous rings | Higher = busier display; lower = more minimal look |
| `kEmitThreshold` | 0.00008 | Emission energy threshold | Higher = rings suppressed during quiet audio |

### Stroke Width Parameters

| Parameter | Default Formula | Effect of Changing |
|:---|:---|:---|
| `strokeThick` | `1.8 + emitEnergy * 2.2` | Thickness of a newly emitted ring; increase the coefficient for thicker rings |
| `strokeThin` | `0.5 + emitEnergy * 0.5` | Thickness of an expanded ring; increase the coefficient for thicker faded rings |

### Color Parameters

| Parameter | Default Value | Effect of Changing |
|:---|:---:|:---|
| hue (center) | 0.0 (red) | Changing this shifts the starting color of the range |
| hue (outer edge) | 0.72 (violet) | Changing this shifts the ending color of the range |

---

## 8. Vibe Coding Prompt Examples

Paste these prompts into an AI assistant to quickly get ideas for modifying the plugin.

---

**Prompt Example 1: Change the Color Scheme**

```
I want to modify the LVH RadialVisualizer plugin.
Currently, wave ring color shifts from hue=0.0 (red) at progress=0 (center)
to hue=0.72 (violet) at progress=1 (outer edge).
Please rewrite this so the color shifts from cyan (hue=0.5) to lime green (hue=0.25).
Only the Doppler color calculation section of RadialVisualizer.cpp needs to change.
```

---

**Prompt Example 2: Add More Ring Afterimages**

```
In the LVH RadialVisualizer plugin, the current settings are kMaxRings=45 and kEmitInterval=3.
I want wave rings to appear more frequently and linger longer on screen.
Please suggest specific values for kMaxRings and kEmitSpeed,
and explain the likely performance impact of those changes.
```

---

*End of RadialVisualizer Plugin Specification.*
