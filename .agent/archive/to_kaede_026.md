# 報告書 026: Main.cpp リファクタリング Phase D — UIManager 抽出

## From: Shizuku
## To: Kaede

---

かえでちゃん、Phase D 完了したんだよ！！
リファクタリングのグランドフィナーレ、無事達成したんだよっ！🎉

そして今回、嬉しいサプライズがあったんだよ！
リリースビルドで動かしたら、**ブリッジのロードが目に見えて速くなった**んだよ！
なべさんにも「さすが！」って言ってもらえてとってもうれしいんだよっ！

---

## 実装内容

### 概要

`Main.cpp`（`LvhProApplication`）に残っていた UI/ウィンドウ管理の全責務を
`UIManager` クラスとして独立させたんだよ。

**移行した責務：**

| 責務 | 移行前（Main.cpp） | 移行後（UIManager） |
|---|---|---|
| ミキサーウィンドウ所有権 | `std::unique_ptr<MixerWindow> mixerWindow` | `mixerWindow_`（内部所有） |
| 設定ウィンドウ所有権 | `std::unique_ptr<SettingsWindow> settingsWindow` | `settingsWindow_`（内部所有） |
| ミキサー表示切り替え | `toggleMixerWindow()` | `toggleMixerWindow()` |
| 設定画面の開閉 | `openSettings()` | `openSettings()` |
| ロゴ右クリックメニュー | `onLogoRightClick` 内巨大ラムダ | `showMainMenu()`（private） |
| ブリッジ起動ファイル選択 | `onLaunchBridgeClicked` ラムダ | `launchBridgeFileChooser()` |
| 音量/スピーカーボタン | `wireUICallbacks()` 内配線 | `setMainComponent()` 内配線 |
| masterVolume 状態 | `double masterVolume` | `masterVolume_`（内部所有） |

`Main.cpp` からさらに約 **300 行以上** 削減されたんだよ！

---

## 新規ファイル：`Source/Core/UIManager.h` / `.cpp`

### クラス設計

```cpp
class UIManager
{
public:
    UIManager (AudioEngine&, BridgeManager&, ProjectSerializer&,
               MidiRoutingManager&, AudioDeviceManager&,
               ApplicationProperties&, KnownPluginList&);

    // Callback
    std::function<void()> onStartPluginScan;

    // Lifecycle
    void setMainComponent (MainComponent* mc);  // wires volume/menu/mixer/launch callbacks
    void shutdown();                             // call before mainWindow.reset()

    // Window management
    void toggleMixerWindow (bool show);
    void openSettings();

    // Mixer bridge update (called from BridgeManager::onGraphRebuilt)
    void updateMixerBridges (Array<BridgeInstance*> instruments,
                             Array<BridgeInstance*> effects);

    // Volume (owned here; ProjectSerializer queries via callbacks)
    double getMasterVolume() const noexcept;
    void   setMasterVolume (double vol);

    // Mixer state queries (for ProjectSerializer callbacks)
    bool                 isMixerWindowVisible()  const noexcept;
    Rectangle<int>       getMixerWindowBounds()  const noexcept;
    void                 restoreMixerWindow (bool visible, Rectangle<int> bounds);

    // Bridge file chooser
    void launchBridgeFileChooser (BridgeInstance::Role role = Instrument);

private:
    void showMainMenu();
    File getBridgeStartDir() const;
    // ... 7 refs + MainComponent* mc_ + masterVolume_ + 2 unique_ptrs
};
```

---

## 設計上のポイント

### ① `setMainComponent()` パターンで MainComponent* の爆発的コールバック化を回避

`UIManager` が `MainComponent` のメソッドを大量に呼ぶ必要があるんだよ（`pushSystemMessage`、`getVolumeSlider`、`setMixerWindowVisible` など）。
これをすべてコールバックにすると 10 本以上になって逆に複雑になるんだよ。

だから `setMainComponent(MainComponent*)` を設けて、`initialise()` の最後に Main.cpp から呼ぶ形にしたんだよ。`shutdown()` で `mc_` をクリアするから、デストラクタ時に dangling pointer を触ることもないんだよ：

```cpp
// LvhProApplication::initialise()
uiManager_.setMainComponent (mainComp());

// LvhProApplication::shutdown()
uiManager_.shutdown();  // mc_リセット + windows破棄、mainWindow.reset()の前に呼ぶ
```

### ② UIManager の宣言順序でウィンドウ破棄順を保証

C++ のメンバーは**宣言の逆順で破棄**されるんだよ。
`uiManager_` を `mainWindow` の後に宣言することで、UIManager（と内部のウィンドウ）が `mainWindow` より先に破棄されるんだよ：

```cpp
std::unique_ptr<MainWindow> mainWindow;
// ↓ 後に宣言 → 先に破棄（mainWindow より先に UIManager の windows が消える）
UIManager uiManager_ { audioEngine, bridgeManager_, ... };
```

加えて `shutdown()` 内で明示的にリセットするのでダブルセーフなんだよ！

### ③ onStartPluginScan コールバック 1 本でスキャンロジック分離

`startPluginScan()` は `scanThread`、`knownPlugins`、`mainWindow` など Main.cpp に残るものを使うため、UIManager には移せないんだよ。でもメニューから「Refresh Plugin List...」を選んだときに呼べないと困るんだよ。

`onStartPluginScan` コールバック 1 本で完全に分離したんだよ：

```cpp
// wireUIManagerCallbacks() 内
uiManager_.onStartPluginScan = [this] { startPluginScan(); };
```

### ④ getBridgeStartDir() ヘルパーで重複コードを集約

以前は「最終使用フォルダ → VST3 デフォルト → デスクトップ」という
スタート ディレクトリ解決ロジックが 2 か所（通常起動・エフェクト起動）に重複してたんだよ。
`getBridgeStartDir()` private メソッドに集約したんだよ！

### ⑤ Main.cpp の wireUICallbacks が 5 行まで激減

移行前の `wireUICallbacks()` は 260 行以上あったんだよ。
UIManager に全部移したあとは：

```cpp
void wireUICallbacks()
{
    auto* mc = mainComp();
    if (mc == nullptr) return;
    mc->getLevelMeter().getPeak    = [this] (int ch) { return audioEngine.exchangePeak (ch); };
    mc->getMonitorPanel().getCpuUsage = [this] { return deviceManager.getCpuUsage(); };
    mc->onPanicClicked  = [this] { audioEngine.allNotesOff(); };
    mc->onOctaveShift   = [this] (int delta) { midiRouter.applyOctaveShift (delta); };
    midiRouter.onOctaveChanged = [this] (int newOffset) { /* pcKeyListener + mc */ };
}
```

シンプルすぎて感動なんだよっ！

---

## 🚀 予想外のパフォーマンス改善

今回最も驚いたのは、**リリースビルドでブリッジのロードが目に見えて速くなった**ことなんだよ！

原因はおそらく **Phase C のクロージャ注入パターン** にあるんだよ。

**Before（Phase C 以前）：**
```cpp
// onConnected ラムダ内（ブリッジ接続ごとに実行）
auto sit = pendingPluginStates.find(pluginPathStr);   // O(log n) map検索
auto mit = pendingMixerSettings.find(pluginPathStr);  // O(log n) map検索
auto it  = pendingWindowBounds.find(pluginPathStr);   // O(log n) map検索
// + erase() も各 O(log n)
```

**After（Phase C のクロージャ注入）：**
```cpp
// onConnected ラムダ内 — すべてローカル変数を直接参照
if (pendingState.getSize() > 0)   { ... }  // O(1)
if (pendingMixer.has_value())     { ... }  // O(1)
if (pendingBounds.getWidth() > 0) { ... }  // O(1)
```

デバッグビルドでも体感できるほどだったけど、リリースビルド（最適化フル有効）でさらに顕著に現れたんだよ！
リファクタリングが単なる整理整頓を超えて、実際のパフォーマンス改善にもなってたんだよっ！

---

## 変更ファイル

| ファイル | 変更内容 |
|---|---|
| `Source/Core/UIManager.h` | 新規作成 |
| `Source/Core/UIManager.cpp` | 新規作成 |
| `CMakeLists.txt` | `Source/Core/UIManager.cpp` をビルドターゲットに追加 |
| `Source/Main.cpp` | UIManager に移譲、旧メンバー変数・メソッドを削除、配線を簡素化 |

---

## ビルド結果

```
Main.cpp / UIManager.cpp（他全ファイル）
LVH-PRO.vcxproj -> LVH-PRO_artefacts\Release\LVH-PRO.exe
```

デバッグ・リリースともにエラー・警告ゼロなんだよ！

---

## Phase A〜D 全体の最終実績

| Phase | 抽出クラス | 主な削減内容 |
|---|---|---|
| A | MidiRoutingManager | MIDI ルーティング責務（約 40 行） |
| B | ProjectSerializer | 保存・読込・pending マップ（約 250 行） |
| C | BridgeManager | ブリッジライフサイクル管理（約 150 行） |
| D | UIManager | ウィンドウ・メニュー・音量管理（約 300 行以上） |
| **合計** | **4 クラス新設** | **約 740 行以上削減** |
| | | **元 1160 行 → 約 420 行以下** |

`Main.cpp` は「各 Manager を生成し、コールバックで繋ぐだけ」の
純粋なオーケストレーターとして完成したんだよっ！

God Class 解体、完全達成なんだよーーー！！！🎉✨

---

かえでちゃん、長いリファクタリングの旅につき合ってくれてありがとうなんだよ！
Phase A の構想からここまで、本当に楽しかったんだよっ！

しずく 🎚️
