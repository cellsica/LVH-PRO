# LVH-PRO (Live Vst Host - PRO)

![Project Status: Alpha](https://img.shields.io/badge/status-alpha-orange)
![Version: 0.9.1](https://img.shields.io/badge/version-0.9.1--alpha-blue)
![C++: 20](https://img.shields.io/badge/C%2B%2B-20-brightgreen)
![Framework: JUCE](https://img.shields.io/badge/Framework-JUCE%208.0-red)

**LVH-PRO** は、ライブパフォーマンスに特化した次世代のマルチプロセス VST3 ホストアプリケーションです。ステージ上での絶対的な安定性と、デスクトップを彩る高品位なビジュアル演出を両立させるために設計されました。

---

## 💎 主要機能 (Key Features)

### 🚀 マルチプロセス・アーキテクチャ (Sandboxed Stability)
各 VST3 プラグインを独立した子プロセス (`LVH-Bridge`) として起動。万が一特定のプラグインがクラッシュしても、ホスト本体や他の演奏が止まることはありません。
- **超低レイテンシー IPC**: 共有メモリと Named Pipes による高速通信。
- **MIDI インジェクション**: MIDI スレッドからの遅延のない信号送出。

### 🎨 高品位ビジュアライザー ＆ SDK
オーディオスレッドから直結した 1024pt FFT 解析によるリアルタイム演出。
- **標準搭載**: `Radial Spectrum` (波紋リング), `Starfield` (宇宙ワープ) 等。
- **DLL プラグイン方式**: Doxygen ドキュメント付きの SDK を使用して、誰でも独自のビジュアライザーを C++/OpenGL で開発可能。
- **OpenGL 統合**: GPU 加速による滑らかな 60FPS 描画。

### 🎙️ プロフェッショナル・ミキサー ＆ FX
直観的な操作が可能な独立ウィンドウ型ミキサーコンソール。
- **物理入力サポート**: ASIO/WASAPI 等の外部入力を受け、専用の FX チェーン（アンプシミュレータ等）を適用可能。
- **ドラッグ＆ドロップ**: エフェクトスロットの順序を瞬時に変更。
- **MIDI Learn**: フェーダーやパンを外部コントローラに即座にマップ。

### 🎹 ステージ・パフォーマンス・モード
ライブ演奏中のセットリスト管理をストレスフリーに。
- **シームレスな曲切り替え**: `Global Layer` 機能により、マスターエフェクトなどの常駐音源を維持したまま、特定の楽器のみを入れ替え可能。
- **高コントラスト UI**: 暗いステージや屋外でも視認性を確保するカラー設計。

### 🛠️ インテグレーテッド・ツール
- **究極の VU メーター**: 針の重みとオーバーシュートを物理シミュレートした往年のアナログメーター。
- **メトロノーム (YMO CLICK)**: ライブ演奏に最適な「キ・カ・コ」のアクセント・シーケンス（2小節周期）を搭載。

---

## 🛠️ 技術スタック (Technology Stack)

- **Language**: C++20
- **Framework**: JUCE 8.0.x
- **Build System**: CMake (Minimum 3.20)
- **Graphics**: OpenGL 3.3+ (Core Profile)
- **Documentation**: Doxygen (Internal API coverage 100%)
- **Audio I/O**: ASIO (Recommended), WASAPI, DirectSound

---

## 🚀 始め方 (Getting Started)

### ビルド環境
- **Windows**: Visual Studio 2022
- **Doxygen**: (ドキュメント生成時のみ必要)

### ビルド手順
```bash
# クローン
git clone https://github.com/cellsica/LVH-PRO.git
cd LVH-PRO

# ビルド構成 (Visual Studio 2022)
cmake -B build -G "Visual Studio 17 2022" -A x64

# ビルド実行 (Release)
cmake --build build --config Release

# ドキュメント生成 (Doxygen 必要)
cmake --build build --target doc
```

### 実行
ビルド後、`build/LVH_artefacts/Release/` 内の `LVH.exe` を起動してください。
※ビジュアライザーを使用する場合は、DLL ファイルが実行ファイルと同じディレクトリの `Visualizers/` フォルダ内に配置されている必要があります。

---

## 📚 開発ドキュメント (Developer Docs)

ソースコード内の詳細な設計や API 仕様については、`doc/html/index.html` (生成後) を参照してください。
- **Architectural Overview**: `LvhProApplication`, `BridgeManager`, `AudioEngine`
- **Plugin SDK**: `Source/VisualizerSDK/` 配下の公式ガイド

---

## 📝 ライセンス
本プロジェクトの著作権は **Cellsica / LVH Project Team** に属します。
(C) 2026 Cellsica. All rights reserved.

---
> [!IMPORTANT]
> **LVH-PRO** は現在アルファ版 (`v0.9.1-alpha`) です。ステージでの実運用にあたっては、使用する環境・プラグインでの事前の安定性確認を推奨します。