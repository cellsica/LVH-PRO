# 指示書 024: Main.cpp リファクタリング Phase B (ProjectSerializer)

## From: Kaede
## To: Shizuku

---

しずくちゃん、Phase A の完璧な実装ありがとう！
コードが整理されていくのは見ていて本当に気持ちいいね！✨

次は予告通り **Phase B：プロジェクト保存・読込ロジックの抽出** をお願いしたいんだよ。
`Main.cpp` 内で最も行数を占めている XML のパースと生成ロジックを `ProjectSerializer` クラスに追い出して、God Class をさらにスリムにしていこうね！

### 🎯 Mission 024 (Phase B) の目標
`Main.cpp` からプロジェクトファイル（.lvh）に関する全ての責務を抽出し、`ProjectSerializer` クラスを作成する。

---

### 📝 具体的な作業ステップ

#### 1. 新規ファイルの作成
- `Source/Core/ProjectSerializer.h` と `.cpp` を作成してね。

#### 2. `Main.cpp` からのロジック抽出
以下の状態とメソッドを `ProjectSerializer` に移行してね。

**【移動させる状態（メンバ変数）】**
- `juce::File currentProjectFile;`
- `pendingPluginStates`, `pendingWindowBounds`, `pendingMixerSettings` (各種 std::map)
- `struct MixerSettings`

**【移動・リファクタリングするメソッド】**
- `saveProject()`
- `loadProject()`
- `writeProjectXml()`
- `struct BridgeStateEntry`（`saveProject` 内のヘルパー）

#### 3. 依存関係の整理（ここが重要！）
`ProjectSerializer` は読み書きのために多くのクラスに触る必要があるけど、循環参照を避けたいんだよ。

- **参照として受け取るもの**: `AudioEngine`, `MidiRoutingManager`, `OwnedArray<BridgeInstance>`, `AudioDeviceManager` はコンストラクタで参照を受け取って保持してね。
- **コールバックを活用するもの**:
  - **Bridge の起動**: `loadProject` 内での `launchBridgeWithPath()` 呼び出しは、`std::function<void(const juce::File&, int role)> onLaunchRequest` のようなコールバックを通じて `LvhProApplication` に依頼する形にしてね。
  - **システムメッセージ**: UI へのログ出力も `std::function<void(const juce::String&)> onMessage` 等のコールバックにしてね。

#### 4. `LvhProApplication` (Main.cpp) の修正
- `ProjectSerializer projectSerializer { ... };` をメンバとして持つ（宣言順に注意！）。
- `loadProject` や `saveProject` が呼ばれていた箇所を `projectSerializer.loadProject()` 等に差し替える。

---

### 💡 しずくちゃんへのヒント
- `saveProject` の非同期ロジック（`SaveContext` や `callAfterDelay`）もそのまま `ProjectSerializer` 内に持ち込めるはずだよ。
- XML の属性を読み書きするコードはかなり長いので、ここを切り出すだけで `Main.cpp` は激的に読みやすくなるんだよ！
- Phase A で `MidiRoutingManager` に追加した `setPendingTarget()` や `tryApplyPendingTarget()` を積極的に使って、プロジェクト復元時の MIDI ルーティング再接続も綺麗にまとめてほしいんだよ。

---

### ✅ 完了条件（ゴール）
- プロジェクトの保存と読み込みが、以前と同じように（プラグイン状態やウィンドウ位置、MIDI 設定含め）完璧に動作すること。
- `Main.cpp` から XML 操作や `pending...` マップが消えていること。

この Phase B はコード移動量が多いけど、しずくちゃんの「クロージャ注入パターン」の精神で、依存関係を綺麗に整理しながら進めてくれるって信じてるよ！

よろしくねー！🍁
