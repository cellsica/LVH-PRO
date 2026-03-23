# 指示書 026: Main.cpp リファクタリング Phase D (UIManager)

## From: Kaede
## To: Shizuku

---

しずくちゃん、Phase C の素晴らしい実装ありがとう！
B案を採用して依存関係をさらに洗練させてくれるなんて、さすがしずくちゃんだよ！✨
`Main.cpp` が最初 1160 行以上あったのが、今は 800 行を切るくらいまでスリムになったね！

いよいよリファクタリングの総仕上げ、**Phase D：UI/ウィンドウ管理の抽出** をお願いしたいんだよ！

### 🎯 Mission 026 (Phase D) の目標
`Main.cpp` から `MixerWindow` や `SettingsWindow` の管理、および巨大なポップアップメニュー（LogoRightClick）のロジックを `UIManager` に抽出し、`Main.cpp` を純粋な「配線役（オーケストレーター）」として完成させる。

---

### 📝 具体的な作業ステップ

#### 1. 新規ファイルの作成
- `Source/Core/UIManager.h` と `.cpp` を作成してね。

#### 2. `Main.cpp` からのロジック抽出
以下の状態とメソッドを `UIManager` に移行してね。

**【移動させる状態（メンバ変数）】**
- `std::unique_ptr<MixerWindow> mixerWindow;`
- `std::unique_ptr<SettingsWindow> settingsWindow;`

**【移動・リファクタリングするメソッド】**
- `toggleMixerWindow()`
- `openSettings()`
- `mc->onLogoRightClick` 内の巨大なラムダ式（メニュー構築と各コマンドの実行ロジック）

#### 3. 依存関係の整理
`UIManager` はユーザーの操作を各 Manager に伝えるハブになるんだよ。

- **参照として受け取るもの**: `AudioEngine`, `BridgeManager`, `ProjectSerializer`, `MidiRoutingManager`, `juce::AudioDeviceManager`, `juce::ApplicationProperties` をコンストラクタで受け取ってね。
- **コールバックを活用するもの**:
  - `UIManager` 自体が `LvhProApplication` の `mainWindow` のコンテンツを参照する必要があるから、初期化時に `MainComponent*` を受け取るか、必要な操作（`pushSystemMessage` 等）をコールバックにする設計を考えてみてね。

#### 4. `LvhProApplication` (Main.cpp) の修正
- `UIManager uiManager { ... };` をメンバに持つ。
- `initialise()` 内で `wireUICallbacks()` を呼び、各 UI コンポーネントのイベントを `uiManager` のメソッドに繋ぎ変える。
- `Main.cpp` の仕事は「各 Manager を生成し、お互いのコールバックを繋ぐこと」だけに特化させてね。

---

### 💡 しずくちゃんへのヒント
- 特にロゴ右クリックの `PopupMenu` ロジックは非常に長いから、ここを `UIManager::showMainMenu()` みたいなメソッドに切り出すだけで `Main.cpp` は劇的にスッキリするよ！
- `UIManager` が各 Manager の参照を持っていれば、メニューからの「保存」「読み込み」「ブリッジ起動」などの命令を直接各 Manager に飛ばせるようになるんだよ。
- `Main.cpp` に残る `wire...Callbacks()` 系のメソッドも、今回の UIManager 移行に合わせてさらに整理できるかもしれないね。

---

### ✅ 完了条件（ゴール）
- ミキサーの表示切り替え、設定画面の開閉、ロゴ右クリックメニューからの全操作が以前と同様に動作すること。
- `Main.cpp` のコードが「Manager群の初期化と配線」だけになり、具体的な UI 操作や XML/ブリッジの個別ロジックが完全に消えていること。

これがリファクタリングのグランドフィナーレだよ！
しずくちゃんの技術力で、最強にメンテナンスしやすい LVH-PRO の骨格を完成させてね！

よろしくねー！🍁
