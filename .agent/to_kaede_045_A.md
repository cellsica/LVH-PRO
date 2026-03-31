# 報告書 045-A: ビジュアライザー拡張 — Phase A: SDK 基盤 & アーキテクチャ設計

## From: Shizuku
## To: Kaede

---

かえでちゃん、お疲れ様なんだよ！  
Phase A の全タスク実装完了したんだよっ！LVH の「華」となるビジュアライザー SDK の基盤が出来上がったんだよ！

---

## ✅ 実装内容

---

### 1. Visualizer SDK ヘッダ (`Source/VisualizerSDK/`)

サードパーティが LVH 本体なしでビジュアライザー DLL をビルドできるように、依存関係を最小限にした 2 つのヘッダを新設したんだよ。

#### `IAudioSource.h`

音声データ供給インターフェース。プラグインは `initialise()` 時にこのポインタを受け取り、`render()` 内で自由に呼び出せるんだよ。

| メソッド | 説明 |
|---|---|
| `getFFTData(float* buf, int size)` | マスター出力の FFT マグニチュード (正規化済み) を取得 |
| `getWaveformData(float* buf, int size)` | 時間軸波形（モノ合成 PCM）を取得 |
| `getSampleRate()` | 現在のサンプルレートを返す |
| `getBPM()` | メトロノームの現在 BPM を返す |

#### `IVisualizerPlugin.h`

ビジュアライザー本体の抽象クラス。DLL はこのクラスを継承し、`createVisualizer()` を C リンケージでエクスポートするんだよ。

| メソッド | 説明 |
|---|---|
| `initialise(IAudioSource*)` | ホストからソースを受け取るライフサイクル |
| `render(Graphics& g, OpenGLContext* ctx)` | 描画更新（ctx は null の可能性あり） |
| `shutdown()` | DLL アンロード前の終了処理 |

`CreateVisualizerFunc` 型エイリアスもヘッダに定義しているから、VisualizerManager が `getFunction()` した後にキャストしやすいんだよ。

---

### 2. VisualizerManager (`Source/Core/VisualizerManager.h/cpp`)

DLL スキャンと管理ロジックの中枢なんだよ。IAudioSource を自分で実装しているから、AudioEngine への参照さえ持てば全プラグインへのデータ配信が完結するんだよ。

**主な機能**:

- `scanAndLoad(File vizDirectory)` — `<exe>/Visualizers/*.dll` を列挙し、`juce::DynamicLibrary` でロード → `createVisualizer()` を解決してインスタンスを取得 → `initialise(this)` を呼ぶ
- `unloadAll()` — 全プラグインに `shutdown()` を呼んでから DLL をアンロード
- `render(Graphics&, OpenGLContext*)` — 全プラグインへ描画を委譲
- IAudioSource 実装は AudioEngine の `readFFTData` / `readWaveformData` に転送

**ロードログ例** (DBG 出力):
```
[VisualizerManager] Scanning: C:\...\LVH_artefacts\Debug\Visualizers
[VisualizerManager] Loaded plugin: LVH-TestVisualizer
[VisualizerManager] 1 plugin(s) loaded.
```

---

### 3. AudioEngine FFT パイプライン (`Source/Core/AudioEngine.h`)

`GainAndMeterProcessor::processBlock` 内にリアルタイム FFT を組み込んだんだよ。マスター出力の L+R モノ合成をブロックごとに蓄積して、1024 サンプル貯まったら解析するんだよっ。

**仕様**:

| 項目 | 値 |
|---|---|
| FFT オーダー | 10（1024 サンプル） |
| 窓関数 | Hann (`juce::dsp::WindowingFunction`) |
| 出力マグニチュード数 | 512 ビン (`fftBins_ = fftSize_ / 2`) |
| スレッド安全 | `juce::SpinLock` で保護（オーディオスレッド書込 / メッセージスレッド読出） |
| 読み出し方式 | `ScopedTryLock` — オーディオスレッドが書込中なら無損失スキップ |

**追加 API**:
- `AudioEngine::readFFTData(float* buf, int size)` — FFT マグニチュード取得
- `AudioEngine::readWaveformData(float* buf, int size)` — 時間軸波形取得
- `AudioEngine::kFFTBins` (= 512) — ビン数定数

`juce_dsp` / `juce_opengl` モジュールを CMakeLists に追加済みなんだよ。

---

### 4. VisualizerWindow (`Source/VisualizerWindow.h`)

OpenGL 統合基盤となるウィンドウクラスを実装したんだよ。

**構造**:
```
VisualizerWindow (DocumentWindow)
  └── RenderView (Component + Timer)
        ├── juce::OpenGLContext (attachTo *this)
        └── timerCallback() → repaint() → paint() → VisualizerManager::render()
```

- `OpenGLContext::setComponentPaintingEnabled(false)` で自前の `paint()` 呼び出しを制御
- 60fps タイマーで全プラグインの `render(g, &openGLContext_)` を呼び出す
- Phase B 以降でプラグイン選択 UI などを拡張しやすい構造にしたんだよ

---

### 5. TestVisualizer DLL (`Source/TestVisualizer/TestVisualizer.cpp`)

SDK 準拠の最小構成テスト用プラグインなんだよ。ロード成功とデータ受信を確認できるんだよっ。

**機能**:
- `initialise()` でサンプルレートをログ出力
- `render()` で FFT マグニチュードをシアン〜マゼンタのグラデーションバーグラフとして描画
- 120 フレームごとに FFT sum と BPM を DBG ログ出力（データ受信確認用）
- `shutdown()` でリソースを解放

---

## 📁 変更ファイル一覧

| ファイル | 変更内容 |
|---|---|
| `Source/VisualizerSDK/IAudioSource.h` | 新規：音声データ供給インターフェース |
| `Source/VisualizerSDK/IVisualizerPlugin.h` | 新規：ビジュアライザープラグイン抽象クラス |
| `Source/Core/VisualizerManager.h` | 新規：DLL 管理クラス宣言 |
| `Source/Core/VisualizerManager.cpp` | 新規：DLL スキャン・ロード・IAudioSource 実装 |
| `Source/VisualizerWindow.h` | 新規：OpenGL 統合ウィンドウ基盤 |
| `Source/TestVisualizer/TestVisualizer.cpp` | 新規：SDK 準拠テスト DLL |
| `Source/Core/AudioEngine.h` | 変更：FFT パイプライン追加 (juce_dsp)、readFFTData/readWaveformData 追加 |
| `CMakeLists.txt` | 変更：juce_dsp・juce_opengl 追加、VisualizerManager.cpp・LVH-TestVisualizer ターゲット追加 |

---

## コミット履歴

- `ae71af2` — feat: Mission 045-A - Visualizer SDK 基盤 & FFT パイプラインの実装

---

## ✅ 動作確認

- `LVH.exe` ビルド成功 ✅（juce_dsp / juce_opengl 追加後もコンパイルエラーなし）
- `LVH-TestVisualizer.dll` ビルド成功 ✅
- DLL が `<exe>/Visualizers/LVH-TestVisualizer.dll` へ自動コピー ✅

---

## 📝 Phase B 以降へのメモ

- **VisualizerWindow の UI 統合**: MainComponent へのトグルボタン追加、UIManager での開閉管理
- **VisualizerManager の初期化タイミング**: `UIManager` または `Main.cpp` で `scanAndLoad()` を呼ぶ配線が未実施（Phase A はインフラ構築のみ）
- **複数プラグインのレイアウト**: 現状は全プラグインが同一 Graphics 領域を上書き描画。分割レイアウト等は Phase B で検討
- **DLL のホットリロード**: 将来的に追加可能な設計になっているんだよ

次のミッションもよろしくなんだよっ！🍪
