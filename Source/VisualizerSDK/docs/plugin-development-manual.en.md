# LVH Visualizer Plugin SDK — Plugin Development Manual

**SDK Version**: v1.0  
**Last Updated**: 2026-03-31

---

## 1. Overview

The LVH Visualizer Plugin SDK lets you add custom visualizers to the LVH application.  
Visualizers are implemented as **Windows DLLs (.dll)** and placed in a designated folder — LVH automatically discovers and loads them on startup.

### How it works

```
LVH.exe
  └─ VisualizerManager
       ├─ LVH-RadialVisualizer.dll   ← bundled official plugin
       ├─ LVH-StarfieldVisualizer.dll
       └─ YourPlugin.dll             ← place your DLL here
```

On startup LVH scans every DLL inside `<LVH.exe folder>/Visualizers/`, locates those that export `createVisualizer()`, and loads them automatically.

---

## 2. SDK Interface Reference

### 2.1 `IVisualizerPlugin` — the plugin itself

```cpp
// Source/VisualizerSDK/IVisualizerPlugin.h
class IVisualizerPlugin {
public:
    virtual ~IVisualizerPlugin() = default;

    // Called once immediately after the DLL is loaded.
    // Store the source pointer; use it inside render().
    virtual void initialise(IAudioSource* source) = 0;

    // Called every frame (~60 fps).
    // g covers the entire visualizer window.
    // openGLContext is reserved for future use; may be nullptr.
    virtual void render(juce::Graphics& g, juce::OpenGLContext* openGLContext) = 0;

    // Called before the DLL is unloaded. Release any resources here.
    virtual void shutdown() = 0;

    // Preferred window size (optional — defaults to 500×500).
    virtual int getPreferredWidth()  const { return 500; }
    virtual int getPreferredHeight() const { return 500; }
};

// Type alias for the DLL export function
using CreateVisualizerFunc = IVisualizerPlugin*(*)();
```

### 2.2 `IAudioSource` — audio data supply

Use the `IAudioSource*` received in `initialise()` to obtain real-time audio data.

| Method | Return | Description |
|---|---|---|
| `getFFTData(float* buf, int size)` | void | Writes FFT magnitude data into `buf`. Pass `size = 512`. Values are normalised (typically 0.0–0.1) |
| `getWaveformData(float* buf, int size)` | void | Writes mono-summed time-domain PCM. `size` up to 1024 |
| `getSampleRate()` | double | Current sample rate (e.g. 44100.0) |
| `getBPM()` | double | Current metronome BPM |

**FFT data characteristics**:
- FFT size: 1024 points, Hann window applied
- Output bins: 512 (pass `size = 512`)
- bin[0] ≈ 43 Hz, bin[480] ≈ 20.6 kHz
- Typical magnitude range: silence < 0.00005, normal playing 0.0001–0.005, loud 0.01+

### 2.3 DLL entry point

Your DLL **must** export the following function:

```cpp
extern "C" __declspec(dllexport) IVisualizerPlugin* createVisualizer()
{
    return new YourVisualizer();
}
```

> **Note**: The instance allocated with `new` will be `delete`d by LVH after calling `shutdown()`.

---

## 3. Development Environment Setup

### 3.1 Required tools

| Tool | Version | Purpose |
|---|---|---|
| Windows 10/11 (64-bit) | — | Target platform |
| Visual Studio 2022 | Community or higher | C++ compiler (MSVC) |
| CMake | 3.22+ | Build system |
| JUCE | 7.0.x | UI and graphics library |
| Git | — | Source control (optional) |

### 3.2 Getting JUCE

```
https://juce.com/get-juce/
```

Extract to any path (e.g. `C:/devdir/JUCE`).

### 3.3 Getting the SDK headers

Copy the `Source/VisualizerSDK/` folder from the LVH repository:

```
VisualizerSDK/
├── IAudioSource.h
└── IVisualizerPlugin.h
```

These two headers are everything you need to build a plugin. The LVH source code is not required.

---

## 4. Creating a Plugin Project

### 4.1 Folder layout

```
MyVisualizer/
├── CMakeLists.txt
├── MyVisualizer.cpp
└── VisualizerSDK/          ← SDK headers copied from LVH
    ├── IAudioSource.h
    └── IVisualizerPlugin.h
```

### 4.2 CMakeLists.txt template

```cmake
cmake_minimum_required(VERSION 3.22)
project(MyVisualizer VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# Adjust this path to wherever you installed JUCE
add_subdirectory("C:/devdir/JUCE" JUCE)

add_library(MyVisualizer SHARED
    MyVisualizer.cpp
)

target_include_directories(MyVisualizer PRIVATE
    .   # so that #include "VisualizerSDK/..." resolves correctly
)

target_link_libraries(MyVisualizer
    PRIVATE
        juce::juce_core
        juce::juce_graphics
        juce::juce_gui_basics
)

target_compile_definitions(MyVisualizer
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

if(MSVC)
    target_compile_options(MyVisualizer PRIVATE /utf-8)
endif()

# Automatically copy the built DLL into LVH's Visualizers folder
set(LVH_VISUALIZERS_DIR "C:/path/to/LVH/Visualizers")
add_custom_command(TARGET MyVisualizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LVH_VISUALIZERS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:MyVisualizer>"
        "${LVH_VISUALIZERS_DIR}/MyVisualizer.dll"
)
```

### 4.3 Minimal plugin implementation

```cpp
// MyVisualizer.cpp
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "VisualizerSDK/IVisualizerPlugin.h"
#include "VisualizerSDK/IAudioSource.h"
#include <array>

class MyVisualizer : public IVisualizerPlugin
{
public:
    void initialise(IAudioSource* source) override
    {
        source_ = source;
    }

    void render(juce::Graphics& g, juce::OpenGLContext*) override
    {
        if (source_ == nullptr) return;

        // Fetch FFT data
        std::array<float, 512> fft{};
        source_->getFFTData(fft.data(), 512);

        // Background
        g.fillAll(juce::Colour(0xff000011));

        // Draw 32 spectrum bars
        auto bounds = g.getClipBounds().toFloat();
        const int   bars = 32;
        const float barW = bounds.getWidth() / bars;

        for (int i = 0; i < bars; ++i)
        {
            int   binStart = i * (512 / bars);
            int   binEnd   = binStart + (512 / bars);
            float val = 0.f;
            for (int b = binStart; b < binEnd; ++b) val += fft[b];
            val /= (binEnd - binStart);

            float barH = juce::jmin(bounds.getHeight(), val * 8000.f);
            g.setColour(juce::Colour::fromHSV(0.55f + val * 20.f, 0.8f, 1.f, 1.f));
            g.fillRect(i * barW, bounds.getBottom() - barH, barW - 1.f, barH);
        }
    }

    void shutdown() override { source_ = nullptr; }

    int getPreferredWidth()  const override { return 520; }
    int getPreferredHeight() const override { return 300; }

private:
    IAudioSource* source_ = nullptr;
};

extern "C" __declspec(dllexport) IVisualizerPlugin* createVisualizer()
{
    return new MyVisualizer();
}
```

---

## 5. Build Instructions

### 5.1 Command-line build

```bat
rem Generate project files
cmake -B build -G "Visual Studio 17 2022" -A x64

rem Build in Release configuration
cmake --build build --config Release
```

This produces `build/Release/MyVisualizer.dll` and copies it to the LVH `Visualizers/` folder automatically.

### 5.2 Building with Visual Studio IDE

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
```

Open `build/MyVisualizer.sln` in Visual Studio, select **Release | x64**, and build.

---

## 6. Deployment and Verification

### 6.1 Where to place the DLL

```
<LVH.exe folder>/
└── Visualizers/
    └── MyVisualizer.dll   ← here
```

### 6.2 Verifying it works

1. Launch LVH
2. Click the **VIS** button in the toolbar to open the visualizer window
3. If the plugin loaded successfully, the System Log panel will show:  
   `[VisualizerManager] Loaded plugin: MyVisualizer`

### 6.3 Multiple plugins

In the current version all plugins in `Visualizers/` render simultaneously into the same window.  
Draw order is alphabetical by filename (later = drawn on top).

---

## 7. Debugging Tips

### Understanding FFT magnitude values

| Situation | Typical magnitude |
|---|---|
| True silence | < 0.00005 |
| Soft playing | 0.0001 – 0.0005 |
| Normal playing | 0.0005 – 0.005 |
| Loud playing | 0.005 – 0.05 |

Use `juce::DBG()` for quick logging:

```cpp
if (++frameCount_ % 120 == 0)
    juce::DBG("FFT avg: " + juce::String(avg));
```

### Common problems

| Symptom | Likely cause | Fix |
|---|---|---|
| DLL not loaded | `createVisualizer` not exported | Check `extern "C" __declspec(dllexport)` |
| Nothing drawn | `source_` is nullptr | Verify `initialise()` is being called |
| Crash on load | Missing JUCE module linkage | Check `target_link_libraries` in CMakeLists |
| Garbled log output | UTF-8 flag missing | Add `/utf-8` compiler option |

---

## 8. Constraints and Notes

- Build as **64-bit (x64)**. 32-bit is not supported.
- `render()` is called from the **message thread**. Do not touch the audio thread from inside it.
- `getFFTData()` and `getWaveformData()` are **thread-safe** (protected by SpinLock).
- `initialise()` and `shutdown()` are called from the message thread.
- Do **not** call `delete` on your plugin instance — LVH does this after `shutdown()`.

---

*LVH Visualizer Plugin SDK — © LVH Project*
