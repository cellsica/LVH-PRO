# 開発準備資料：軽量VST/VSTiホスト（64bit版）プロトタイプ開発

## 1. プロジェクト概要
既存のホスト（Element, SAVIHost等）における不安定さや、多機能ゆえの動作の重さを解消するため、機能を最小限に絞った「軽量・確実・高速」なWindows用VSTホストを開発する。

### ターゲット・マイルストーン
* **プラットフォーム:** Windows 10/11 (64bit)
* **対応規格:** VST3 / VST2 (64bitのみ)
* **主要機能:**
    * 特定フォルダからのプラグイン手動登録（お気に入り管理機能）
    * MIDIコントローラー信号の受信、VSTiでの発音、オーディオ出力
    * 最小限のGUI（プラグイン純正Editorの表示、基本設定パネルのみ）
    * キーボードショートカットによる登録済みプラグインの即時切り替え

## 2. 推奨技術スタック
* **言語:** C++ (C++17以上推奨)
* **フレームワーク:** **JUCE Framework** (ver 7.x 以上)
    * 理由：`AudioProcessorGraph` による信号ルーティングの容易さと、Windowsオーディオドライバ（ASIO/WASAPI）の抽象化レベルが高いため。
* **ビルドツール:** Visual Studio 2022 / CMake

## 3. 事前準備・リソースURL
開発開始にあたって、以下のダウンロードおよびドキュメントの参照を推奨する。

### A. フレームワーク（必須）
* **JUCE公式サイト:** [https://juce.com/get-juce/](https://juce.com/get-juce/)
* **JUCE GitHub Repository:** [https://github.com/juce-framework/JUCE](https://github.com/juce-framework/JUCE)

### B. 技術リファレンス
* **JUCE API Reference:** [https://docs.juce.com/master/index.html](https://docs.juce.com/master/index.html)
* **Tutorial: Audio Processor Graph:** [https://docs.juce.com/master/tutorial_audio_processor_graph.html](https://docs.juce.com/master/tutorial_audio_processor_graph.html)
    * ※本プロジェクトの中核となるクラス。

### C. テスト用ツール
* **ASIO4ALL:** [https://www.asio4all.org/](https://www.asio4all.org/)
    * 標準オーディオデバイスでの低遅延検証用。

## 4. 実装ガイドライン（プロトタイプ構成）

### ① オーディオ・MIDIエンジン
`juce::AudioDeviceManager` を使用し、デバイスの入出力を一括管理。ASIO対応をデフォルトとする。

### ② プラグイン・ハンドリング
`juce::AudioPluginFormatManager` でVST3/2形式をロード。
`juce::KnownPluginList` を利用して「お気に入り」パスを保持し、JSON形式でローカルに保存・復元する。

### ③ シグナルパスの構築
`juce::AudioProcessorGraph` をノードベースで構築する。
* **Input:** `MidiInput`
* **Node:** `AudioPluginInstance` (VSTi)
* **Output:** `AudioOutput`
プラグインの切り替えは、グラフ内のノードを差し替える手法をとることで、オーディオエンジンの再起動を防ぎ安定性を確保する。

### ④ ショートカット・コントロール
`juce::KeyListener` を介して数値キー等の入力を監視。
保存された「お気に入りリスト」のインデックスに基づき、プラグインを即座にロード・置換する機能を実装する。

## 5. チームへの特記事項
* **シンプルさの維持:** 複雑なミキサーやエフェクトチェーンは実装対象外とする。
* **GUIの最小化:** 可能な限り