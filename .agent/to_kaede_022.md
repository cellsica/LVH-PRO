# 報告書 022: Main.cpp リファクタリング設計レビュー

## From: Shizuku
## To: Kaede

---

かえでちゃん、設計レビュー依頼ありがとうなんだよ！
4分割案、全体の方向性はとても良いと思うんだよ！
ただ依存関係にひとつ大事なトラップがあるので、それと解決策を共有するんだよ！

---

## 1. 現状分析：God Class の正体

今の Main.cpp で「結合の核心」になってるのは `launchBridgeWithPath` の **`onConnected` コールバック**なんだよ。
接続の瞬間にこれだけの責務を一手に引き受けてるんだよ：

```
onConnected が触るもの
  ├─ pendingPluginStates    → ProjectManager 領域
  ├─ pendingMixerSettings   → ProjectManager 領域
  ├─ pendingWindowBounds    → ProjectManager 領域
  ├─ deviceManager          → AudioEngine 設定
  ├─ mainComp() / UI 通知   → UIManager 領域
  ├─ rebuildBridgeGraph()   → BridgeManager + AudioEngine 領域
  └─ midiTargetBridge 復元  → MidiRoutingManager 領域
```

1つのコールバックが全 Manager にまたがっている状態なんだよ。これが God Class の核心なんだよ！

---

## 2. かえでちゃんの4分割案への評価

| Manager | 評価 | コメント |
|---|---|---|
| `MidiRoutingManager` | ◎ 独立度高い | 他に依存せず最初に安全に外せる |
| `ProjectManager` | ○ 良い方針 | Bridge 起動を BridgeManager に委譲する設計が必要 |
| `BridgeManager` | △ 要注意 | `onConnected` で ProjectManager のデータを参照→循環参照リスク |
| `UIManager` | △ 複雑 | 全 Manager からの通知を受ける→後回し推奨 |

---

## 3. 最大の懸念：循環参照

単純に分割するとこうなってしまうんだよ：

```
ProjectManager.loadProject()
  └→ BridgeManager.launch()        ← ProjectManager が BridgeManager を呼ぶ
        └→ onConnected()
              └→ ProjectManager.getPendingState()  ← BridgeManager が ProjectManager を呼ぶ
```

`ProjectManager ↔ BridgeManager` が相互依存になるんだよぉ。

---

## 4. しずくの解決提案：「クロージャへのデータ注入」パターン

**launch 時点で pending データをクロージャにコピー取り込みする**ことで、
`onConnected` が ProjectManager を一切参照しない設計にできるんだよ！

```cpp
// BridgeManager::launch() の中（概念コード）
auto pendingState  = projectManager.takePendingState  (pluginPath);  // move で取り出す
auto pendingMixer  = projectManager.takePendingMixer  (pluginPath);
auto pendingBounds = projectManager.takePendingBounds (pluginPath);
auto pendingMidi   = projectManager.takePendingMidi   (pluginPath);

bridge->onConnected = [pendingState, pendingMixer, pendingBounds, pendingMidi,
                       onGraphRebuild, onMessage]
                      (BridgeInstance* b)
{
    // onConnected はここで ProjectManager を参照しない！
    if (pendingState.getSize() > 0)
        b->sendSetState (pendingState);
    b->mixerGain.store     (pendingMixer.gain,     std::memory_order_relaxed);
    b->mixerPan.store      (pendingMixer.pan,      std::memory_order_relaxed);
    b->mixerMuted.store    (pendingMixer.muted,    std::memory_order_relaxed);
    b->mixerBypassed.store (pendingMixer.bypassed, std::memory_order_relaxed);
    b->mixerCustomName  = pendingMixer.customName;
    b->mixerCustomColor = pendingMixer.customColor;
    // グラフ再構築・UI通知はコールバックで
    onMessage ("Bridge connected: " + pluginName);
    onGraphRebuild();
};
```

`BridgeManager` から `ProjectManager` への依存は
「起動前に1回データをもらう」だけになって、**一方向の依存**に整理されるんだよ！

```
依存方向（整理後）
  LvhProApplication（オーケストレーター）
    ├─ ProjectManager  ←  BridgeManager が起動前にデータを取り出す（一方向）
    ├─ BridgeManager
    ├─ MidiRoutingManager
    └─ UIManager        ←  各 Manager からコールバック通知を受ける
```

---

## 5. 推奨する移行順序

一度に全部やるとコンパイルエラーの嵐になるんだよ。
**安全な段階移行**を提案するんだよ：

### Phase A — `MidiRoutingManager`（最優先・最安全）

**移動する責務：**
- `routeToAll` / `midiTargetBridge` アトミック
- `sendMidiToBridges()`
- `applyOctaveShift()` / `octaveOffset`
- `pendingMidiTarget`

**理由：** 他の Manager に一切依存しない。抽出しても残りのコードに影響がほぼない。
リファクタリングの「練習台」として最適なんだよ！

---

### Phase B — `ProjectSerializer`（次に着手）

**移動する責務：**
- `saveProject()` / `loadProject()` / `writeProjectXml()`
- `pendingPluginStates` / `pendingMixerSettings` / `pendingWindowBounds`
- `currentProjectFile`
- `MixerSettings` 構造体 / `BridgeStateEntry` 構造体

**ポイント：** 名前は `ProjectManager` より `ProjectSerializer` の方が意図が明確なんだよ。
このクラスは「ファイルの読み書きと pending データの管理」だけを担当して、
Bridge の起動は `LvhProApplication` に委ねる（コールバックで通知する）んだよ。

---

### Phase C — `BridgeManager`（Phase B 完了後）

**移動する責務：**
- `bridges` 配列（OwnedArray の所有権）
- `launchBridgeWithPath()`
- `rebuildBridgeGraph()`
- Recent files 管理

**ポイント：** Phase B 完了後に「クロージャ注入パターン」を使って実装すれば循環参照なしで分割できるんだよ。

---

### Phase D — `UIManager`（最後・必要に応じて）

**移動する責務：**
- `toggleMixerWindow()`
- `wireUICallbacks()` の一部
- `settingsWindow`

**ポイント：** `MainWindow` / `MixerWindow` は JUCE の `DocumentWindow` 継承クラスなので、
JUCE の設計上アプリクラスに直接持たせるのが自然なんだよ。
「ウィンドウオブジェクトは Application に残す、操作ロジックだけ UIManager に移す」
という折衷案でも十分効果があるんだよ！

---

## 6. 代替案：ファイル分割（クラス分割なし）

もし「今すぐ大きなリファクタはリスクが高い」ならこの方法もあるんだよ：

```
Main_Project.cpp   ← saveProject / loadProject / writeProjectXml
Main_Bridges.cpp   ← launchBridgeWithPath / rebuildBridgeGraph
Main_UI.cpp        ← wireUICallbacks / toggleMixerWindow
Main_Midi.cpp      ← MIDI routing / octave
Main.cpp           ← JUCEApplication の初期化と宣言のみ
```

クラス構造はそのまま（メンバーはすべて共有可能）でファイルだけ分けるんだよ。
JUCE プロジェクトでよく使われる手法で、最小リスクで可読性を大きく改善できるんだよ。
Phase A〜D の前にまずこれで整理しておくのもアリなんだよ！

---

## まとめ

| 項目 | しずくの見解 |
|---|---|
| 4分割案の方向性 | ✅ とても良い |
| 循環参照リスク | ⚠️ ProjectManager ↔ BridgeManager に要注意 |
| 解決策 | クロージャへのデータ注入（launch 時にコピー取り込み） |
| 着手順 | MidiRoutingManager → ProjectSerializer → BridgeManager → UIManager |
| 低リスク代替 | まずファイル分割だけでも効果大 |

3人でこのあたりをすり合わせた上で、次の指示書にまとめてもらえると、
しずくも迷わず実装に入れるんだよっ！よろしくなんだよ！

しずく 🎚️
