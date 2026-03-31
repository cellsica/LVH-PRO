# LVH Visualizer Plugin SDK — プラグイン開発マニュアル

**対象バージョン**: SDK v1.0  
**最終更新**: 2026-03-31

---

## 1. 概要

LVH Visualizer Plugin SDK を使うと、LVH アプリケーションにカスタムビジュアライザーを追加できます。  
ビジュアライザーは **Windows DLL (.dll)** として実装し、所定のフォルダに配置するだけで LVH が自動的に読み込みます。

### 仕組み

```
LVH.exe
  └─ VisualizerManager
       ├─ LVH-RadialVisualizer.dll   ← 公式同梱プラグイン
       ├─ LVH-StarfieldVisualizer.dll
       └─ YourPlugin.dll             ← ここに自作 DLL を置く
```

LVH は起動時に `<LVH.exe のフォルダ>/Visualizers/` 内の全 DLL をスキャンし、`createVisualizer()` を持つものをロードします。

---

## 2. SDK インターフェース仕様

### 2.1 `IVisualizerPlugin` — プラグイン本体

```cpp
// Source/VisualizerSDK/IVisualizerPlugin.h
class IVisualizerPlugin {
public:
    virtual ~IVisualizerPlugin() = default;

    // LVH がプラグインをロードした直後に一度だけ呼ばれる。
    // source を保存しておき、render() 内で使用する。
    virtual void initialise(IAudioSource* source) = 0;

    // 毎フレーム(~60fps)呼ばれる描画コールバック。
    // g はウィンドウ全体をカバーする juce::Graphics オブジェクト。
    // openGLContext は将来の拡張用。現在は nullptr の場合あり。
    virtual void render(juce::Graphics& g, juce::OpenGLContext* openGLContext) = 0;

    // DLL アンロード前に呼ばれる。リソースを解放する。
    virtual void shutdown() = 0;

    // ウィンドウの推奨サイズ（省略可。デフォルト: 500×500）
    virtual int getPreferredWidth()  const { return 500; }
    virtual int getPreferredHeight() const { return 500; }
};

// DLL エクスポート関数の型エイリアス
using CreateVisualizerFunc = IVisualizerPlugin*(*)();
```

### 2.2 `IAudioSource` — 音声データ供給

`initialise()` で受け取った `IAudioSource*` を通じて、リアルタイムの音声データを取得できます。

| メソッド | 戻り値 | 説明 |
|---|---|---|
| `getFFTData(float* buf, int size)` | void | FFT マグニチュードデータを `buf` に書き込む。`size` は通常 512。値は正規化済み (0.0〜0.1 程度) |
| `getWaveformData(float* buf, int size)` | void | 時間軸波形（モノ合成 PCM）を書き込む。`size` は最大 1024 |
| `getSampleRate()` | double | 現在のサンプルレート (例: 44100.0) |
| `getBPM()` | double | メトロノームの現在 BPM |

**FFT データの特性**:
- FFT サイズ: 1024 点、Hann 窓関数適用
- 出力ビン数: 512 (`size` に 512 を渡す)
- bin[0] ≈ 43 Hz、bin[480] ≈ 20.6 kHz
- 典型的な値域: 無音 < 0.00005、通常演奏 0.0001〜0.005、大音量 0.01〜

### 2.3 DLL エントリーポイント

DLL は以下の関数を必ずエクスポートしてください。

```cpp
extern "C" __declspec(dllexport) IVisualizerPlugin* createVisualizer()
{
    return new YourVisualizer();
}
```

> **注意**: `new` で生成したインスタンスは LVH が `shutdown()` 呼び出し後に `delete` します。

---

## 3. 開発環境のセットアップ

### 3.1 必要なツール

| ツール | バージョン | 用途 |
|---|---|---|
| Windows 10/11 (64-bit) | — | 実行環境 |
| Visual Studio 2022 | Community 以上 | C++ コンパイラ (MSVC) |
| CMake | 3.22 以上 | ビルドシステム |
| JUCE | 7.0.x | UI・グラフィクスライブラリ |
| Git | — | ソース管理 (任意) |

### 3.2 JUCE の入手

```
https://juce.com/get-juce/
```

ダウンロード後、任意のパスに展開します（例: `C:/devdir/JUCE`）。

### 3.3 LVH SDK ヘッダーの取得

LVH リポジトリから `Source/VisualizerSDK/` フォルダをコピーします。

```
Source/VisualizerSDK/
├── IAudioSource.h
└── IVisualizerPlugin.h
```

これらのヘッダーだけで DLL を開発できます。LVH 本体のソースは不要です。

---

## 4. プロジェクトの作成

### 4.1 フォルダ構成

```
MyVisualizer/
├── CMakeLists.txt
├── MyVisualizer.cpp
└── VisualizerSDK/          ← LVH から持ってきた SDK ヘッダー
    ├── IAudioSource.h
    └── IVisualizerPlugin.h
```

### 4.2 CMakeLists.txt テンプレート

```cmake
cmake_minimum_required(VERSION 3.22)
project(MyVisualizer VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# JUCE のパスを指定（環境に合わせて変更）
add_subdirectory("C:/devdir/JUCE" JUCE)

add_library(MyVisualizer SHARED
    MyVisualizer.cpp
)

target_include_directories(MyVisualizer PRIVATE
    .   # VisualizerSDK/ フォルダが見つかるよう、プロジェクトルートを追加
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

# ビルド後に LVH の Visualizers フォルダへ自動コピー（パスは環境に合わせて変更）
set(LVH_VISUALIZERS_DIR "C:/path/to/LVH/Visualizers")
add_custom_command(TARGET MyVisualizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LVH_VISUALIZERS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:MyVisualizer>"
        "${LVH_VISUALIZERS_DIR}/MyVisualizer.dll"
)
```

### 4.3 最小構成プラグイン実装例

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

        // FFT データを取得
        std::array<float, 512> fft{};
        source_->getFFTData(fft.data(), 512);

        // 背景を塗りつぶす
        g.fillAll(juce::Colour(0xff000011));

        // 32本のバーを描画（シンプルなスペアナ）
        auto bounds = g.getClipBounds().toFloat();
        const int   bars  = 32;
        const float barW  = bounds.getWidth() / bars;

        for (int i = 0; i < bars; ++i)
        {
            // FFT ビンの平均を取る
            int binStart = i * (512 / bars);
            int binEnd   = binStart + (512 / bars);
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

## 5. ビルド手順

### 5.1 コマンドラインビルド

```bat
rem ビルドフォルダを作成して CMake を実行
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

完了すると `build/Release/MyVisualizer.dll` が生成され、LVH の `Visualizers/` フォルダへ自動コピーされます。

### 5.2 Visual Studio でビルドする場合

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
```

生成された `build/MyVisualizer.sln` を Visual Studio で開き、**Release x64** でビルドします。

---

## 6. DLL の配置と動作確認

### 6.1 配置場所

```
<LVH.exe のフォルダ>/
└── Visualizers/
    └── MyVisualizer.dll   ← ここに置く
```

### 6.2 動作確認

1. LVH を起動する
2. ツールバーの **VIS** ボタンをクリックしてビジュアライザーウィンドウを開く
3. プラグインが読み込まれると自動的に描画が始まる
4. システムログパネルに `[VisualizerManager] Loaded plugin: MyVisualizer` と表示されれば成功

### 6.3 複数プラグインの共存

現在のバージョンでは、`Visualizers/` フォルダ内の全プラグインが同時に同じウィンドウへ描画されます。  
描画順はファイル名のアルファベット順です（後から描画されるものが上に重なります）。

---

## 7. デバッグのヒント

### FFT 値のスケール感

デバッグ時は `juce::DBG()` でログ出力が便利です。

```cpp
// 120 フレームごとに FFT の合計値をログ出力
if (++frameCount_ % 120 == 0)
    juce::DBG("FFT sum: " + juce::String(fftSum));
```

### よくある問題

| 症状 | 原因 | 対処 |
|---|---|---|
| DLL が読み込まれない | `createVisualizer` がエクスポートされていない | `extern "C" __declspec(dllexport)` を確認 |
| 何も描画されない | `source_` が nullptr | `initialise()` が呼ばれているか確認 |
| クラッシュする | JUCE モジュールのリンク漏れ | CMakeLists の `target_link_libraries` を確認 |
| 文字化けするログ | UTF-8 設定漏れ | `/utf-8` コンパイルオプションを追加 |

---

## 8. 制限事項・注意点

- DLL は **64-bit (x64)** でビルドしてください。32-bit は非対応です。
- `render()` はメッセージスレッドから呼ばれます。オーディオスレッドに触れないでください。
- `getFFTData()` / `getWaveformData()` はスレッドセーフです（SpinLock 保護済み）。
- `initialise()` と `shutdown()` はメッセージスレッドから呼ばれます。
- プラグインのデストラクタは `shutdown()` 呼び出し後に LVH が実行します。自分で `delete` しないでください。

---

*LVH Visualizer Plugin SDK — © LVH Project*
