# 指示書 037: Global Layer (Stage Set 拡張)

## From: Kaede
## To: Shizuku

---

しずくちゃん、お疲れ様！
Mission 036 の Settings 刷新と、終了処理の安定化、本当に完璧だったよっ！
なべさんも「固まらなくなった！」って喜んでたよ。紧急バグ修正もありがとう！✨

次は、いよいよプロ向け機能の目玉、**Mission 035-C: Global Layer** をお願いしたいんだ。
ライブ中に曲を切り替えても、メインの音色やマスターエフェクトを途切れさせないための重要なアップデートだよ！

---

## 🎯 目的
Stage Set (.stg) の **Slot 0 (最初のスロット) を「Global プロジェクト」として常駐化** させる。
これにより、Slot 0 にロードしたブリッジは、他のスロット（曲）をロードしても破棄されずに残り、常に背後で鳴らし続けることができるようにする。

## ✅ 作業内容

### 1. BridgeInstance に isGlobal フラグを追加
- **ファイル**: `Source/BridgeInstance.h`, `Source/BridgeInstance.cpp`
- `BridgeInstance` クラスに `bool isGlobal` メンバを追加してね（デフォルトは `false`）。
- コンストラクタや初期化時に設定できるようにしてほしいな。

### 2. BridgeManager::clearBridges(bool keepGlobal) の実装
- **ファイル**: `Source/Core/BridgeManager.h`, `Source/Core/BridgeManager.cpp`
- 現在の `clearBridges()` を拡張して、「Global フラグが立っているブリッジを残す」オプションを作ってほしいんだ。
- `rebuildBridgeGraph()` に渡すブリッジリストも、Global と Local が混ざった状態になるように調整してね。

### 3. ProjectSerializer の拡張
- **ファイル**: `Source/Core/ProjectSerializer.h`, `Source/Core/ProjectSerializer.cpp`
- `loadProject()` 時、起動するブリッジに `isGlobal` フラグを伝播できるようにしてね。
- `onProjectResetRequired` コールバックに `keepGlobal` 引数を追加して、不要なときだけ Global も消すように制御しよう。

### 4. StageManager のロジック更新
- **ファイル**: `Source/Core/StageManager.cpp`
- セットリストの Slot 0 が読み込まれた際、それを Global として扱うフラグや内部状態を設けてね。
- Slot 1, 2... と切り替わる際は、`LvhProApplication` 経由で「Global 以外のリセット」を要求するように連携しよう。

### 5. ミキサー UI での視覚的分離
- **ファイル**: `Source/MixerWindow.h` (または関連クラス)
- Global 由来のチャンネルストリップは、ヘッダーに「GLOBAL」と表示するか、枠線の色を変えるなどして、曲ごとのチャンネルと区別できるようにしてほしいな。

---

## 💡 茜ちゃんからのアドバイス（ルーティング）
- 「Global なマスターエフェクトは、Local な曲の音の後ろに繋がると一番使いやすいよ！ グラフ構築の順序に気をつけてねっ！」とのこと！

---

しずくちゃん、この「常駐レイヤー」ができれば LVH-PRO はライブで最強のホストになるはずだよ！
よろしくねっ！🍪✨
