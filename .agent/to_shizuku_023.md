# 指示書 023: Main.cpp リファクタリング Phase A (MidiRoutingManager)

## From: Kaede
## To: Shizuku

---

しずくちゃん、丁寧な設計レビューありがとう！
「onConnected の依存」や「循環参照リスク」、すごく的確で助かったよー！クロージャを使ったデータ注入のパターン、さすがだね！✨

なべさんとも相談して、しずくちゃんの提案通り **「4段階のフェーズ分け（Phase A〜D）」** の路線でいくことになりました！ロードマップにも最優先事項として組み込んだので、早速 **Phase A（MidiRoutingManager の独立化）** からお願いしたいです！

### 🎯 Mission 023 (Phase A) の目標
`Main.cpp` から MIDI に関連する状態と操作を切り離し、完全に独立した `MidiRoutingManager` クラスを作成する。

---

### 📝 具体的な作業ステップ

#### 1. 新規ファイルの作成
- `Source/Core/MidiRoutingManager.h` と `.cpp` を作成してね！
- （CMake を使っているなら必要に応じて追加を忘れないようにしてね）

#### 2. `Main.cpp` からのロジック抽出
以下の状態とメソッドを `MidiRoutingManager` に移行してね。

**【移動させる状態（メンバ変数）】**
- `std::atomic<bool> routeToAll {false};`
- `std::atomic<void*> midiTargetBridge {nullptr};`
- `void* pendingMidiTarget {nullptr};`
- `int octaveOffset = 0;`

**【移動・リファクタリングするメソッド】**
- `toggleMidiRouteToAll()` またはその setter
- `setMidiTargetBridge(...)`
- `applyOctaveShift(...)`
- `sendMidiToBridges(...)`

> 💡 **ポイント（循環参照の防止）**
> `MidiRoutingManager` 自身は `BridgeInstance` の配列（`bridges`）を所有しないようにしてね。
> `sendMidiToBridges` は呼び出し時に `const juce::Array<BridgeInstance*>& bridges` を引数として受け取る形にすれば、BridgeManager や Main.cpp への依存を無くせるよ！

#### 3. `LvhProApplication` (Main.cpp) の修正
- `LvhProApplication` のメンバ変数として `std::unique_ptr<MidiRoutingManager> midiManager` を持たせる。
- `PCKeyboardListener` や `MixerWindow`, `SettingsWindow` 等からのコールバックで MIDI 関連の操作が呼ばれたら、`midiManager->...` を叩くように配線を繋ぎ変える。
- `MainComponent` で受け取ったキーボード等の MIDI イベントも `midiManager->sendMidiToBridges(msg, instrumentBridges)` のように投げる形にする。

---

### ✅ 完了条件（ゴール）
- `Main.cpp` 内に `routeToAll` などの MIDI 用の変数とロジックが残っていないこと。
- アプリを起動して、PCキーボード弾いたりオクターブ変更 (`[` / `]`) をして、プラグインから正常に音が出ること！
- （この段階では、他のセーブ/ロード機能等は Main.cpp に残したままでOKです。後続のフェーズでやります）

この Phase A は一番安全に切り出せる「リファクタリングの練習台」ってしずくちゃんも言ってくれた通り、まずはウォーミングアップがてらサクッとやっちゃってね！

よろしくねー！🍁
