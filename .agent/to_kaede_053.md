# Mission 053 完了報告 — Processor Plugin Dispatcher（全フェーズ）+ v0.14.0-alpha リリース

**担当:** しずく (Claude)  
**完了日:** 2026-04-04  
**ブランチ:**  
- `feature/mission-053-phase-a-processor-dispatcher` → `develop` マージ済み  
- `feature/mission-053-phase-b-dispatcher-ui` → `develop` マージ済み  
- `feature/mission-053-phase-c-persistence-fade` → `develop` マージ済み  
- `feature/fix-vumeter-icon-alignment` → `develop` マージ済み

---

## 実装概要

外部 DLL として配布できるプロセッサープラグイン（`IProcessorPlugin` SDK）の動的ロード・アンロードを、専用の管理 UI から操作できる仕組みを実装しました。  
スキャンとインスタンス生成を明確に分離し、ゲインフェードイン/アウト・永続化・プラグイン側 X ボタンによる自動停止まで含む完全な実装です。

---

## Phase A — ProcessorManager スキャン/起動分離

### 設計方針

| 旧実装 | 新実装 |
|---|---|
| スキャンと同時に DLL ロード | `scanOnly()` でファイルパスのみ記録 |
| 一括起動のみ | `startProcessor(int)` / `stopProcessor(int)` で個別制御 |

### 変更ファイル

| ファイル | 内容 |
|---|---|
| `Source/Core/ProcessorManager.h` | `DiscoveredProcessor` 構造体、`scanOnly()` / `startProcessor()` / `stopProcessor()` / `isRunning()` / `getName()` / `getAccentColour()` / `getActiveInstances()` / `getActiveInstancesExcluding()` / `hasUserRequestedClose()` |
| `Source/Core/ProcessorManager.cpp` | 上記全メソッドの実装 |

### `DiscoveredProcessor` 構造体

```cpp
struct DiscoveredProcessor {
    juce::File   dllFile;
    juce::String stemName;
    std::unique_ptr<juce::DynamicLibrary> library;  // null = stopped
    IProcessorPlugin* instance = nullptr;           // null = stopped
    bool isRunning() const noexcept { return instance != nullptr; }
};
```

---

## Phase B — Processor Dispatcher UI ウィンドウ

### 機能

- ツールバーに「Processor Manager」ボタン（オレンジ、3本バーアイコン）を追加
- `ProcessorDispatcherWindow` を新規作成
  - 発見済み DLL をリスト表示（アクセントカラーバッジ + 名前 + START/STOP トグルボタン）
  - ウィンドウサイズはアイテム数に応じて自動調整

### 変更ファイル

| ファイル | 内容 |
|---|---|
| `Source/ProcessorDispatcherWindow.h` | ウィンドウクラス（新規） |
| `Source/UiCommon.h` | `Icons::processor()` 追加（3本水平バー + コネクタドット） |
| `Source/MainComponent.h/.cpp` | `processorToggleButton` 追加、`onProcessorToggle` コールバック |
| `Source/Core/UIManager.h/.cpp` | `toggleProcessorDispatcher()` / `startProcessor()` / `stopProcessor()` ほか |

### DocumentWindow サイズ問題の解決

`setContentNonOwned()` が `bool` 引数に関わらず ComponentListener を登録し続けるため、ウィンドウ移動のたびにサイズが変動するバグが発生。  
**対策:** `setContentNonOwned` を廃止し `addAndMakeVisible()` + `resized()` オーバーライドで固定サイズを保持。

```cpp
void resized() override
{
    juce::DocumentWindow::resized();
    if (content_ != nullptr)
        content_->setBounds(0, getTitleBarHeight(), fixedW_, fixedH_);
}
```

---

## Phase C — フェードイン/アウト・永続化・プラグイン自己クローズ検出

### フェードイン/アウト

| タイミング | 処理 |
|---|---|
| START 直後 | `setProcessorGain(0.0)` → 80ms 後に `setProcessorGain(1.0)` |
| STOP 直前 | `setProcessorGain(0.0)` → 80ms 後にグラフ再構築 → DLL アンロード |

### 永続化

- `ApplicationProperties` の `"activeProcessors"` キーに起動中プラグイン名をパイプ区切りで保存
- 起動時 `scanOnly()` 後に `restoreActiveProcessors()` を呼び、一致する名前のプラグインを自動 START

### プラグイン自己クローズ検出（Bug 7 修正）

プラグイン側のウィンドウ X ボタン押下を LVH 本体が検知できるよう SDK に新メソッドを追加。

**`IProcessorPlugin` SDK 追加:**
```cpp
virtual bool hasUserRequestedClose() const noexcept { return false; }
```

**`SimpleLooper` 実装:**
```cpp
std::atomic<bool> closeRequested_{ false };

bool hasUserRequestedClose() const noexcept override {
    return closeRequested_.load(std::memory_order_relaxed);
}
// WM_CLOSE ハンドラ内:
self->closeRequested_.store(true, std::memory_order_relaxed);
```

**UIManager ポーリング (500ms):**
```cpp
void UIManager::timerCallback()
{
    for (int i = processorManager_->getNumDiscovered() - 1; i >= 0; --i)
        if (processorManager_->isRunning(i) && processorManager_->hasUserRequestedClose(i))
            stopProcessor(i);
}
```

### 修正したバグ一覧

| # | 症状 | 原因 | 対策 |
|---|---|---|---|
| 1 | 初回表示で高さ不足 | `centreWithSize` でサイズ上書き | `fixedW_/fixedH_` キャッシュ方式に変更 |
| 2 | 移動後に下に空白 | `setContentNonOwned` の ComponentListener ループ | `addAndMakeVisible` + `resized()` 手動レイアウトに変更 |
| 3 | サイズが横方向に拡大 | 同上（別軸） | 同上 |
| 4 | X ボタンで LVH 本体が終了 | `closeButtonPressed()` 内で self を `reset()` | `juce::MessageManager::callAsync` で非同期削除 |
| 5 | STOP 後に LVH が終了 | DLL アンロード後にオーディオスレッドがアクセス | グラフ再構築 → DLL アンロード の順序を明確化（`getActiveInstancesExcluding()` 追加） |
| 6 | STOP 後もボタンが STOP のまま | `refreshStates()` の呼び出し漏れ | 遅延ラムダの末尾に `refreshStates()` 追加 |
| 7 | プラグイン X ボタン後もボタンが STOP のまま | 自己クローズ検知なし | `hasUserRequestedClose()` + タイマーポーリング追加 |

---

## 付帯修正 — VU メーターボタンアイコンの上下位置ずれ

### 症状
VU メーターボタンのアイコンが他のボタンと比べて下寄りに表示されていた。

### 原因
`UiCommon.h` の `vuMeter()` 関数でピボット点 `py` が `a.getBottom() - h * 0.15f` と下寄りに固定されていた。  
（描画コンテンツの中心 = `py - r/2` が `getCentreY()` より `0.16h` 下にズレていた）

### 修正

```cpp
// Before
const float py = a.getBottom() - a.getHeight() * 0.15f;
const float r  = a.getWidth()  * 0.38f;

// After
const float r  = a.getWidth()  * 0.38f;
const float py = a.getCentreY() + r * 0.5f;   // コンテンツ中心 = getCentreY()
```

---

## v0.14.0-alpha リリース

### バージョン更新

| ファイル | 変更内容 |
|---|---|
| `CMakeLists.txt` | `project(LVH VERSION 0.9.0)` → `VERSION 0.14.0` |
| `Source/Main.cpp` | `getApplicationVersion()` が `"0.14.0-alpha"` を返すよう修正 |

### ビルド成果物

- `build/LVH_artefacts/Release/LVH.exe`（v0.14.0-alpha）
- `build/Release/SimpleLooper.dll`

---

> Mission 053 全フェーズ完了なんだよ！  
> プロセッサーを DLL として差し込むだけで LVH から START/STOP できる仕組みが整ったんだよっ！  
> v0.14.0-alpha、リリース準備完了なんだよ！
