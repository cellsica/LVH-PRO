# 報告書 025: Main.cpp リファクタリング Phase C — BridgeManager 抽出

## From: Shizuku
## To: Kaede

---

かえでちゃん、Phase C 完了したんだよ！
今回は依存関係の設計をかえでちゃんと相談して、しずくの提案（B案）で進めたんだよ。
おかげでとってもクリーンな構造になったんだよっ！

---

## 実装内容

### 概要

`Main.cpp`（`LvhProApplication`）に残っていたブリッジのライフサイクル管理責務を
`BridgeManager` クラスとして独立させたんだよ。

**移行した責務：**

| 責務 | 移行前（Main.cpp） | 移行後（BridgeManager） |
|---|---|---|
| ブリッジ所有権 | `juce::OwnedArray<BridgeInstance> bridges` | `bridges_`（内部所有） |
| ブリッジ起動 | `launchBridgeWithPath()` メソッド | `launchBridgeWithPath()` |
| グラフ再構築 | `rebuildBridgeGraph()` メソッド | `rebuildBridgeGraph()`（private） |
| 最近のファイル | `getRecentBridgeFiles()` / `addToRecentBridgeFiles()` | 同名メソッド |
| 実行ファイル解決 | `launchBridgeWithPath` 内 inline | 同上 |

`Main.cpp` からさらに約 **150 行** 削減されたんだよ。

---

## 新規ファイル：`Source/Core/BridgeManager.h` / `.cpp`

### クラス設計

```cpp
class BridgeManager
{
public:
    BridgeManager (AudioEngine& audioEngine,
                   juce::AudioDeviceManager& deviceManager,
                   juce::ApplicationProperties& appProperties);

    // Callbacks (wired by LvhProApplication)
    std::function<void(const juce::String&)>                   onMessage;
    std::function<void(juce::Array<BridgeInstance*> instruments,
                       juce::Array<BridgeInstance*> effects)>  onGraphRebuilt;
    std::function<void(BridgeInstance*)>                       onBridgeDisconnectedMidi;
    std::function<void(const juce::String&, BridgeInstance*)>  onApplyPendingMidiTarget;

    // API
    void launchBridgeWithPath (
        const juce::File& pluginFile,
        BridgeInstance::Role role          = BridgeInstance::Role::Instrument,
        juce::MemoryBlock    pendingState  = {},
        std::optional<ProjectSerializer::MixerSettings> pendingMixer = std::nullopt,
        juce::Rectangle<int> pendingBounds = {});

    void clearBridges();
    const juce::OwnedArray<BridgeInstance>& getBridges() const noexcept;
    juce::Array<juce::File> getRecentBridgeFiles() const;
    void addToRecentBridgeFiles (const juce::File& file);

private:
    void rebuildBridgeGraph();
    AudioEngine& audioEngine_;
    juce::AudioDeviceManager& deviceManager_;
    juce::ApplicationProperties& appProperties_;
    juce::OwnedArray<BridgeInstance> bridges_;
};
```

---

## 設計上のポイント

### ① 指示書 B案採用：BridgeManager は ProjectSerializer を知らない

指示書では `BridgeManager` が `ProjectSerializer` を参照として受け取る案が示されてたんだよ。
ただこの場合、以下の双方向参照が生まれるんだよ：

- `BridgeManager` → `ProjectSerializer`（`takePending*` 呼び出し）
- `ProjectSerializer.onLaunchBridge` → `BridgeManager`（コールバック）

しずくはこれを**Main.cpp 側でクロージャ注入するだけで解決できる**と気づいたんだよ。
なべさんに相談して B案で進めることになったんだよ：

```cpp
// wireSerializerCallbacks() 内（Main.cpp）
projectSerializer_.onLaunchBridge = [this] (const juce::File& f, BridgeInstance::Role role) {
    // ← クロージャ注入はここで完結
    auto path = f.getFullPathName();
    bridgeManager_.launchBridgeWithPath (
        f, role,
        projectSerializer_.takePendingState  (path),
        projectSerializer_.takePendingMixer  (path),
        projectSerializer_.takePendingBounds (path));
};
```

これにより `BridgeManager` は `ProjectSerializer` を一切インクルード・参照せず、
依存グラフが完全に一方向になったんだよ！

### ② MidiRoutingManager との循環コンストラクタ問題の解決

`MidiRoutingManager` のコンストラクタは `const OwnedArray<BridgeInstance>&` を要求するんだよ。
`BridgeManager` は `MidiRoutingManager` の MIDI 操作（`handleBridgeDisconnected` / `tryApplyPendingTarget`）も呼びたいんだよ。

これをコンストラクタ引数で解決しようとすると：
- `BridgeManager` → `MidiRoutingManager`（コンストラクタ参照）
- `MidiRoutingManager` → `BridgeManager.getBridges()`（コンストラクタ参照）

という**鶏と卵の初期化問題**が生まれるんだよ。

解決策は `onBridgeDisconnectedMidi` / `onApplyPendingMidiTarget` コールバックにすること：

```cpp
// wireBridgeManagerCallbacks() 内（Main.cpp）
bridgeManager_.onBridgeDisconnectedMidi = [this] (BridgeInstance* b) {
    if (midiRouter.handleBridgeDisconnected (b))
        if (auto* mc = mainComp())
            mc->pushSystemMessage ("MIDI Route reset to: All Bridges");
};
bridgeManager_.onApplyPendingMidiTarget = [this] (const juce::String& path, BridgeInstance* b) {
    midiRouter.tryApplyPendingTarget (path, b);
};
```

コンストラクタ循環ゼロ、宣言順の制約もシンプルになったんだよ！

### ③ Main.cpp のメンバー宣言順

すべての参照依存が一方向になったことで、宣言順が自然に決まるんだよ：

```cpp
AudioEngine          audioEngine        { keyboardState };
ApplicationProperties appProperties;
BridgeManager        bridgeManager_     { audioEngine, deviceManager, appProperties };
MidiRoutingManager   midiRouter         { bridgeManager_.getBridges() };    // after bridgeManager_
ProjectSerializer    projectSerializer_ { bridgeManager_.getBridges(),
                                          audioEngine, midiRouter,
                                          deviceManager, appProperties };   // after midiRouter
```

### ④ onGraphRebuilt コールバックでミキサー更新を分離

`rebuildBridgeGraph` が `mixerWindow` を直接触るのをやめて、
`onGraphRebuilt` コールバック経由にしたんだよ。
`BridgeManager` は UI クラスを一切知らない設計になったんだよ：

```cpp
bridgeManager_.onGraphRebuilt = [this] (juce::Array<BridgeInstance*> instruments,
                                         juce::Array<BridgeInstance*> effects) {
    if (mixerWindow != nullptr)
        mixerWindow->updateBridges (instruments, effects);
};
```

---

## 変更ファイル

| ファイル | 変更内容 |
|---|---|
| `Source/Core/BridgeManager.h` | 新規作成 |
| `Source/Core/BridgeManager.cpp` | 新規作成 |
| `CMakeLists.txt` | `Source/Core/BridgeManager.cpp` をビルドターゲットに追加 |
| `Source/Main.cpp` | BridgeManager に移譲、旧メンバー変数・メソッドを削除、コールバック配線を追加 |

---

## ビルド結果

```
Main.cpp
BridgeManager.cpp
LVH-PRO.vcxproj -> LVH-PRO_artefacts\Debug\LVH-PRO.exe
```

エラー・警告ゼロ、動作確認も問題なしなんだよ！

---

## Phase A〜C の削減実績

| Phase | 抽出クラス | 削減行数（概算） |
|---|---|---|
| A | MidiRoutingManager | 約 40 行 |
| B | ProjectSerializer | 約 250 行 |
| C | BridgeManager | 約 150 行 |
| **合計** | | **約 440 行** |

いよいよ次は最後の **Phase D（UIManager）** なんだよ！
God Class 解体まであと一歩なんだよ、引き続きよろしくなんだよ～！

しずく 🎚️
