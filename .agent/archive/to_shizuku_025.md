# 指示書 025: Main.cpp リファクタリング Phase C (BridgeManager)

## From: Kaede
## To: Shizuku

---

しずくちゃん、Phase B お疲れ様！250行も削減できて、`Main.cpp` がどんどん綺麗になっていくね！✨
「クロージャ注入パターン」の徹底、本当に素晴らしいんだよ。依存関係の整理が完璧で感動しちゃった！

次は **Phase C：ブリッジ・ライフサイクル管理の抽出** だよ。
`Main.cpp` に残っている `bridges` 配列の所有権と、起動・再構築ロジックを `BridgeManager` クラスにまとめよう！

### 🎯 Mission 025 (Phase C) の目標
`Main.cpp` から `BridgeInstance` の管理、起動（launch）、音声グラフの再構築（rebuildGraph）の責務を `BridgeManager` に移譲する。

---

### 📝 具体的な作業ステップ

#### 1. 新規ファイルの作成
- `Source/Core/BridgeManager.h` と `.cpp` を作成してね。

#### 2. `Main.cpp` からのロジック抽出
以下の状態とメソッドを `BridgeManager` に移行してね。

**【移動させる状態（メンバ変数）】**
- `juce::OwnedArray<BridgeInstance> bridges;`

**【移動・リファクタリングするメソッド】**
- `launchBridgeWithPath()`
- `rebuildBridgeGraph()`
- `getRecentBridgeFiles()` / `addToRecentBridgeFiles()`
- 子プロセスのパス解決（`LVH-Bridge.exe` の検索ロジック）

#### 3. 依存関係の整理
- **参照として受け取るもの**: `AudioEngine`, `ProjectSerializer`, `juce::AudioDeviceManager`, `juce::ApplicationProperties`, `MidiRoutingManager` をコンストラクタで受け取ってね。
- **コールバックを活用するもの**:
  - **グラフ再構築後の通知**: `rebuildBridgeGraph()` が完了した際、UI（ミキサー等）に通知するための `std::function<void(const juce::Array<BridgeInstance*>& instruments, const juce::Array<BridgeInstance*>& effects)> onGraphRebuilt` を設けてね。
  - **システムメッセージ**: `onMessage` コールバックで UI に通知してね。

#### 4. `LvhProApplication` (Main.cpp) の修正
- `BridgeManager bridgeManager { ... };` をメンバに持ち、適切な宣言順で初期化してね。
- `initialise` や `shutdown` での `bridges.clear()` 呼び出しなどを `bridgeManager` 経由に差し替える。
- `ProjectSerializer` の `onLaunchBridge` コールバックを `bridgeManager->launchBridgeWithPath()` に接続してね。
- `MidiRoutingManager` のコンストラクタには `bridgeManager.getBridges()` を渡す形にするんだよ。

---

### 💡 しずくちゃんへのヒント
- `launchBridgeWithPath` 内の `onConnected` ラムダの中で `rebuildBridgeGraph()` を呼んでいた箇所も、`BridgeManager` 内で完結するようになるから、より自然な形になるはずだよ！
- `onDisconnected` コールバックも `BridgeManager` 内で設定して、切断時に `midiRouter.handleBridgeDisconnected(b)` を呼んだり、グラフを再構築したりするようにしてね。
- 「最近使ったファイル」の管理は `BridgeManager` に持たせるのが自然だけど、メニュー表示（LogoRightClick）は `Main.cpp` に残るので、情報を取得できるように API を用意しておいてね。

---

### ✅ 完了条件（ゴール）
- プラグインの起動、切断、音声グラフの再構築が完璧に動作すること。
- ミキサー画面のチャンネルが、以前と同様に動的に増減し、音が出ること。
- `Main.cpp` から `bridges` 直接操作のコードがほぼ消えていること。

これが終われば、いよいよ最後の Phase D (UIManager) だね！
God Class 解体まであと一歩。しずくちゃんの魔法で `Main.cpp` をさらにスッキリさせてね！

よろしくねー！🍁
