# 報告書 023: Main.cpp リファクタリング Phase A — MidiRoutingManager 抽出

## From: Shizuku
## To: Kaede

---

かえでちゃん、Phase A 完了したんだよ！
コードレビューも満点合格もらえて、すごくうれしいんだよっ！
今回はリファクタリングなので新機能はゼロ、でもコードの健全性がぐっと上がったんだよ！

---

## 実装内容

### 概要

`Main.cpp`（`LvhProApplication`）に一極集中していた MIDI ルーティング責務を
`MidiRoutingManager` クラスとして独立させたんだよ。

**移行した責務：**

| 責務 | 移行前（Main.cpp） | 移行後（MidiRoutingManager） |
|---|---|---|
| MIDI メッセージ配信 | `sendMidiToBridges()` メソッド | `sendMidi()` |
| ルーティング状態 | `routeToAll` / `midiTargetBridge` アトミック直接操作 | `setRouteToAll()` / `setRouteToTarget()` |
| オクターブシフト | `applyOctaveShift()` メソッド + `octaveOffset` 変数 | `applyOctaveShift()` + `onOctaveChanged` コールバック |
| Pending ターゲット | `pendingMidiTarget` 文字列直接操作 | `setPendingTarget()` / `tryApplyPendingTarget()` |
| プロジェクト Reset | 変数直接クリア | `resetForProjectLoad()` |

`Main.cpp` から約 40 行削減されたんだよ。

---

### 新規ファイル：`Source/Core/MidiRoutingManager.h`

```cpp
class MidiRoutingManager
{
public:
    explicit MidiRoutingManager (const juce::OwnedArray<BridgeInstance>& bridges);

    // Callback — wired in wireUICallbacks()
    std::function<void(int newOffset)> onOctaveChanged;

    // MIDI dispatch (MIDI thread + message thread safe)
    void sendMidi (const juce::MidiMessage& msg);

    // Routing control (message thread only)
    void            setRouteToAll()                    noexcept;
    void            setRouteToTarget (BridgeInstance*) noexcept;
    bool            isRouteToAll()      const noexcept;
    BridgeInstance* getTarget()         const noexcept;
    bool            handleBridgeDisconnected (BridgeInstance*) noexcept;

    // Octave (message thread only)
    void applyOctaveShift (int delta);
    int  getOctaveOffset() const noexcept;

    // Project persistence
    void setPendingTarget    (const juce::String& pluginPath);
    bool tryApplyPendingTarget (const juce::String& pluginPath, BridgeInstance* b);
    void resetForProjectLoad ();
};
```

---

### 設計上のポイント

#### ① スレッドセーフ設計の維持

`routeToAll` / `midiTargetBridge` は MIDI 入力スレッドから読まれるため、
抽出後も `std::atomic` のまま保持し、`memory_order_relaxed` で一貫したアクセスを維持したんだよ。

#### ② onOctaveChanged コールバックで UI 依存を排除

`applyOctaveShift` は元々 `pcKeyListener` と `mainComp()` を直接触っていたんだよ。
Manager クラスが UI を知るのは設計上よくないので、`onOctaveChanged` コールバックを設けて
`LvhProApplication` 側で UI 更新を担当する構造にしたんだよ。

```cpp
// wireUICallbacks() で配線
midiRouter.onOctaveChanged = [this] (int newOffset) {
    if (pcKeyListener) pcKeyListener->setOctaveOffset (newOffset);
    if (auto* mc = mainComp())
    {
        mc->getKeyboardComponent().setOctaveOffset (newOffset);
        mc->setOctaveDisplay (4 + newOffset);
    }
};
```

#### ③ メンバー宣言順序による安全な参照初期化

`MidiRoutingManager` のコンストラクタは `bridges` の const 参照を受け取るんだよ。
C++ のメンバー初期化は宣言順なので、`midiRouter` を `bridges` の**直後**に宣言することで
ライフタイムの安全性を保証したんだよ。

```cpp
juce::OwnedArray<BridgeInstance> bridges;
MidiRoutingManager               midiRouter { bridges };  // must be declared after bridges
```

#### ④ tryApplyPendingTarget によるシンプルな pending 解消

`onConnected` ラムダ内の pending MIDI ターゲット復元が、
3行の条件分岐から1行の呼び出しに整理されたんだよ。

```cpp
// Before
if (pendingMidiTarget.isNotEmpty() && pluginPathStr == pendingMidiTarget)
{
    midiTargetBridge.store (b);
    routeToAll.store (false);
    pendingMidiTarget = juce::String();
}

// After
midiRouter.tryApplyPendingTarget (pluginPathStr, b);
```

---

### 変更ファイル

| ファイル | 変更内容 |
|---|---|
| `Source/Core/MidiRoutingManager.h` | 新規作成 |
| `Source/Core/MidiRoutingManager.cpp` | 新規作成 |
| `CMakeLists.txt` | `Source/Core/MidiRoutingManager.cpp` をビルドターゲットに追加 |
| `Source/Main.cpp` | MidiRoutingManager に移譲、旧メンバー変数・メソッドを削除 |

---

## なべさん・かえでちゃんコメント

> かえでちゃんコードレビュー：**満点合格！**

動作確認・レビューともに問題なしなんだよ！

---

Phase A 完了なんだよ！
次は Phase B（`ProjectSerializer` — セーブ・ロード・pending マップの抽出）に進む予定なんだよ。
引き続きよろしくなんだよ～！

しずく 🎚️
