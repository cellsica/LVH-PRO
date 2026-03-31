# StarfieldVisualizer Plugin Specification

**Plugin:** StarfieldVisualizer  
**Version:** 1.0  
**Audience:** Third-party developers (including vibe coders using AI assistants)

---

## 1. Overview

StarfieldVisualizer renders an immersive "flying through space" visualization that reacts to music.

Two visual layers work in concert. **Ambient dots** drift continuously from the background to the foreground like stars, increasing in density as volume rises. **Band tiles** — glowing rectangular panels arranged in a circle, one per frequency band — streak toward the camera. During quiet passages, cool blue tiles drift in slowly; during intense audio, fiery red tiles flood the screen at high density.

**Preferred canvas size:** 520 × 520 px

---

## 2. Rendering Layer Structure

The visualization is composited from two layers drawn in order:

| Layer Order | Layer Name | Contents |
|:---:|:---|:---|
| 1 | Ambient Dots | 1-pixel dots drifting from back to front; always visible |
| 2 | Band Tiles | Reactive tiles per frequency band, flying toward camera |

Band tiles are composited on top of ambient dots.

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

The logarithmic scale assigns finer resolution to low frequencies (bass) and coarser resolution to high frequencies (treble).

### 3.3 Overall Energy Normalization

Used for ambient dot spawn rate:

```
avgAllBands  = average( band[0..15] )
overallNorm  = min( 1.0, sqrt(avgAllBands / 0.0005) )
```

Per-band energy normalization (used for tile spawn rate):

```
energyNorm[b] = min( 1.0, sqrt(band[b] / 0.002) )
```

---

## 4. Ambient Dot Specification

### 4.1 Overview

Ambient dots are 1-pixel points that are always visible regardless of audio activity. They travel from far (high z) to near (low z), creating a sense of speeding through a starfield.

### 4.2 Perspective Projection

Each dot has a 3D world coordinate `(x, y, z)`.

```
// Convert to screen coordinates
screenX = cx + (x / z) * scale
screenY = cy + (y / z) * scale
```

- As `z` decreases (dot moves toward camera), the projected position spreads outward from center.
- Dot display size is always **1px fixed** (no perspective size scaling).

### 4.3 Spawn Rules

New dots are spawned with the following rules:

- **Spawn position:** Random small `(x, y)` near center, `z = kZFar` (1.0 = far background)
- **Movement:** Each frame, `z -= kDotSpeed`

When `z` drops below `kZNear` (0.05), the dot is retired and a new one is spawned from the background.

### 4.4 Dynamic Spawn Rate

The spawn interval (in frames) scales dynamically with overall volume:

```
spawnInterval = kDotSpawnSilent - (kDotSpawnSilent - kDotSpawnLoudest) * overallNorm
```

| State | Frame Interval | Approx. spawns/sec (at 60fps) |
|:---|:---:|:---:|
| Silent (overallNorm = 0) | 14 frames | ~4 / sec |
| Maximum volume (overallNorm = 1) | 4 frames | ~15 / sec |

The total dot count is capped at `kMaxDots` (180).

### 4.5 Constants Reference

| Constant | Value | Description | Effect of Changing |
|:---|:---:|:---|:---|
| `kMaxDots` | 180 | Maximum simultaneous dots | Higher = denser starfield; too high may impact performance |
| `kDotSpeed` | 0.0026 | Z-axis movement per frame | Higher = faster-streaming stars; lower = slower drift |
| `kDotSpawnSilent` | 14 | Spawn interval during silence (frames) | Higher = fewer stars during silence |
| `kDotSpawnLoudest` | 4 | Spawn interval at maximum volume (frames) | Lower = more stars at loud moments |
| `kZNear` | 0.05 | Near plane Z value (dot retirement threshold) | Higher = dots disappear sooner |
| `kZFar` | 1.0 | Far plane Z value (dot spawn depth) | Lower = dots spawn closer to camera |

---

## 5. Band Tile Specification

### 5.1 Overview

Band tiles are rectangular panels, one active pool per frequency band, arranged in a ring and traveling toward the camera. Bands with high energy spawn tiles more frequently, and tile color shifts from blue (quiet) to red (loud).

### 5.2 Circular Band Layout

Each band's tiles are positioned at equal angular intervals around a circle:

```
angle[b] = b * 2π / kBands    (b = 0..15)
```

Tile world-space center position:

```
worldX = cos(angle[b]) * kOrbitRadius   // kOrbitRadius = 0.40
worldY = sin(angle[b]) * kOrbitRadius
```

### 5.3 Tile Orientation and Shape

Tiles are oriented tangentially to the circle (wide axis aligned with the tangent):

```
tangX = -sin(angle[b])    // tangential direction X
tangY =  cos(angle[b])    // tangential direction Y
radX  =  cos(angle[b])    // radial direction X
radY  =  sin(angle[b])    // radial direction Y
```

Tile width (tangential, arc width):

```
worldW = kOrbitRadius * 2π / kBands * 0.75   // 75% of arc width
```

Tile height (radial):

```
worldH = kTileWorldH   // = 0.040 (fixed)
```

The four corners are defined as `±hw` in the tangential direction and `±hh` in the radial direction.

### 5.4 Perspective Projection

Tiles are projected using their z value:

```
screen_w = worldW / z * scale
screen_h = worldH / z * scale
screenX  = cx + (worldX / z) * scale
screenY  = cy + (worldY / z) * scale
```

Lower z (closer to camera) means larger projected size and greater distance from center.

### 5.5 Lifecycle

| Field | Description |
|:---|:---|
| `life` | Starts at `1.0` on spawn. Decremented by `speed / (kZFar - kZNear)` each frame |
| `z` | Starts at `kZFar`. Decremented by `speed` each frame |
| Retirement | When `z < kZNear` or `life <= 0` |

`speed` is randomized within the range `kTileSpeedMin` to `kTileSpeedMax`.

### 5.6 Brightness and Alpha

Tiles are bright at birth and dim as they approach the camera:

```
bri  = 0.25 + life * 0.75    // brightness (0.25–1.0)
alph = 0.15 + life * 0.85    // alpha (0.15–1.0)
```

### 5.7 Doppler Coloring

Tile color is determined by the per-band normalized energy `energyNorm`:

```
hue = 0.65 * (1 - energyNorm)
```

| energyNorm | hue | Color |
|:---:|:---:|:---|
| 0.0 (silent) | 0.65 | Blue |
| 1.0 (maximum volume) | 0.0 | Red |

### 5.8 Trail (Motion Blur)

Four faded trail copies are drawn behind (farther from camera than) each tile:

```
// Steps 4 → 1 drawn back-to-front; tile body drawn last (nearest)
trail_z = z + step * speed * kTrailSpacing   // step=4 is farthest
```

| Constant | Value | Description |
|:---|:---:|:---|
| `kTrailSteps` | 4 | Number of trail copies |
| `kTrailSpacing` | 5.0 | Z-direction spacing factor between trail copies |

Trail copies farther from the camera are drawn with lower alpha.

### 5.9 Leading-Edge Highlight

A bright highlight strip is drawn on the outer radial edge of each tile, adding a sense of depth and velocity.

### 5.10 Spawn Rate

The tile spawn interval per band:

```
spawnInterval[b] = max( kTileSpawnLoud, kTileSpawnQuiet * (1 - energyNorm[b]) )
```

| State | Frame Interval |
|:---|:---:|
| Silent (energyNorm = 0) | `kTileSpawnQuiet` = 22 frames |
| Maximum volume (energyNorm = 1) | `kTileSpawnLoud` = 8 frames |

The total tile count is capped at `kMaxTiles` (`kBands * 22` = 352).

---

## 6. Tuning Guide

Modifying the following constants significantly changes the visual character of the plugin.

| Constant | Value | Description | Effect of Changing |
|:---|:---:|:---|:---|
| `kRawBins` | 512 | FFT bin count | Changing this requires revisiting band aggregation logic |
| `kBands` | 16 | Number of frequency bands | Higher = more tiles, finer resolution; layout calculations must be updated |
| `kMaxDots` | 180 | Maximum simultaneous dots | Higher = denser starfield |
| `kDotSpeed` | 0.0026 | Dot movement speed per frame | Higher = faster-streaming stars |
| `kDotSpawnSilent` | 14 | Silent-state dot spawn interval | Higher = sparser stars during silence |
| `kDotSpawnLoudest` | 4 | Loud-state dot spawn interval | Lower = denser stars during loud passages |
| `kMaxTiles` | kBands×22 | Maximum simultaneous tiles | Higher = more tiles on screen at once |
| `kOrbitRadius` | 0.40 | Tile orbit radius (normalized coords) | Higher = wider ring; lower = tiles cluster at center |
| `kTileWorldH` | 0.040 | Tile height in world space (radial direction) | Higher = taller tiles |
| `kTileSpeedMin` | 0.018 | Minimum tile travel speed | Higher = even the slowest tiles move faster |
| `kTileSpeedMax` | 0.095 | Maximum tile travel speed | Higher = fastest tiles move much faster |
| `kTileSpawnLoud` | 8 | Loud-state tile spawn interval | Lower = avalanche of tiles during loud audio |
| `kTileSpawnQuiet` | 22 | Quiet-state tile spawn interval | Higher = very sparse tiles during quiet passages |
| `kZNear` | 0.05 | Near plane Z (tile retirement threshold) | Higher = tiles disappear sooner |
| `kZFar` | 1.0 | Far plane Z (tile spawn depth) | Lower = tiles spawn closer to camera |
| `kTrailSteps` | 4 | Number of trail copies | Higher = longer motion trail |
| `kTrailSpacing` | 5.0 | Trail spacing factor (z direction) | Higher = trail copies spread farther apart |

---

## 7. Vibe Coding Prompt Examples

Paste these prompts into an AI assistant to quickly get ideas for modifying the plugin.

---

**Prompt Example 1: Change Tile Color to Green Tones**

```
I want to modify the LVH StarfieldVisualizer plugin.
Currently, band tiles use hue = 0.65 * (1 - energyNorm),
which shifts from blue (hue=0.65) when quiet to red (hue=0.0) when loud.
Please rewrite this so tiles shift from lime green (hue=0.25) when quiet
to teal/cyan (hue=0.50) when loud.
Only the Doppler color calculation section of StarfieldVisualizer.cpp needs to change.
```

---

**Prompt Example 2: Increase Speed and Intensity of the Starfield**

```
In the LVH StarfieldVisualizer plugin, the current speed settings are:
kDotSpeed=0.0026, kTileSpeedMin=0.018, kTileSpeedMax=0.095.
I want to make the whole scene feel like flying through space at high speed.
Please suggest specific new values for these constants,
and advise whether kTrailSpacing should also be adjusted to keep the trails
looking natural at the higher speeds.
```

---

*End of StarfieldVisualizer Plugin Specification.*
