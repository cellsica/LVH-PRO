# 報告書 024: Main.cpp リファクタリング Phase B — ProjectSerializer 抽出

## From: Shizuku
## To: Kaede

---

かえでちゃん、Phase B 完了したんだよ！
今回は移動するコード量がかなり多かったけど、クロージャ注入パターンのおかげで依存関係がすごくきれいに整理できたんだよっ！

---

## 実装内容

### 概要

`Main.cpp`（`LvhProApplication`）に残っていたプロジェクト保存・読込の全責務を
`ProjectSerializer` クラスとして独立させたんだよ。

**移行した責務：**

| 責務 | 移行前（Main.cpp） | 移行後（ProjectSerializer） |
|---|---|---|
| プロジェクト保存 | `saveProject()` メソッド | `saveProject()` |
| XML 書き込み | `writeProjectXml()` メソッド | `writeProjectXml()` |
| プロジェクト読込 | `loadProject()` メソッド | `loadProject()` |
| pending 状態管理 | `pendingPluginStates` / `pendingMixerSettings` / `pendingWindowBounds` | 同名マップ（`_` サフィックス付き） |
| 現在のプロジェクトファイル | `currentProjectFile` | `currentProjectFile_` |
| MixerSettings 構造体 | `struct MixerSettings`（Main.cpp内） | `struct MixerSettings`（public ネスト型） |

`Main.cpp` から約 **250 行** 削減されたんだよ。

---

## 新規ファイル：`Source/Core/ProjectSerializer.h` / `.cpp`

### クラス設計

```cpp
class ProjectSerializer
{
public:
    struct MixerSettings { float gain, pan; bool muted, bypassed;
                           juce::String customName; juce::Colour customColor; };

    ProjectSerializer (const juce::OwnedArray<BridgeInstance>& bridges,
                       AudioEngine& audioEngine, MidiRoutingManager& midiRouter,
                       juce::AudioDeviceManager& deviceManager,
                       juce::ApplicationProperties& appProperties);

    // Callbacks (wired by LvhProApplication)
    std::function<void(const juce::String&)>                   onMessage;
    std::function<void(const juce::File&, BridgeInstance::Role)> onLaunchBridge;
    std::function<void()>                                       onProjectResetRequired;
    std::function<double()>                                     getMasterVolume;
    std::function<juce::Rectangle<int>()>                       getCoreWindowBounds;
    std::function<bool()>                                       getMixerVisible;
    std::function<juce::Rectangle<int>()>                       getMixerWindowBounds;
    std::function<void(double)>                                 onMasterVolumeChanged;
    std::function<void(juce::Rectangle<int>)>                   onCoreWindowBoundsChanged;
    std::function<void(bool, juce::Rectangle<int>)>             onMixerWindowRestored;

    // Public API
    void saveProject (const juce::File& file);
    void loadProject (const juce::File& file);
    juce::File getCurrentProjectFile() const noexcept;
    void       setCurrentProjectFile (const juce::File& f);

    // Closure injection helpers
    juce::MemoryBlock            takePendingState  (const juce::String& pluginPath);
    std::optional<MixerSettings> takePendingMixer  (const juce::String& pluginPath);
    juce::Rectangle<int>         takePendingBounds (const juce::String& pluginPath);
};
```

---

## 設計上のポイント

### ① クロージャ注入パターンによる循環依存の回避

最大の課題は `loadProject` → `launchBridgeWithPath` → `onConnected` のループだったんだよ。
`onConnected` が `pending*` マップを参照するため、`BridgeManager` が `ProjectSerializer` を
知る必要が生じてしまうという問題があったんだよ。

解決策は `launchBridgeWithPath` の**スポーン前**に `takePending*` で全データを取り出し、
`onConnected` ラムダのクロージャに**値キャプチャ**することなんだよ：

```cpp
// Main.cpp: launchBridgeWithPath() 内
auto pendingState  = projectSerializer_.takePendingState  (pluginPathStr);
auto pendingMixer  = projectSerializer_.takePendingMixer  (pluginPathStr);
auto pendingBounds = projectSerializer_.takePendingBounds (pluginPathStr);

bridge->onConnected = [this, pluginName, pluginPathStr,
                        pendingState  = std::move (pendingState),
                        pendingMixer,
                        pendingBounds] (BridgeInstance* b) mutable {
    // pending データをローカル変数として使用
    // ProjectSerializer への参照は一切不要
};
```

この設計により `onConnected` は `ProjectSerializer` を一切知らず、
循環依存がゼロなんだよっ！

### ② コールバック 10 本で UI 依存を排除

`ProjectSerializer` が直接触ると設計上まずいもの（`mainWindow`, `mixerWindow`, `mainComp()`）は
すべてコールバック経由にしたんだよ。

`wireSerializerCallbacks()` を `Main.cpp` に追加して `initialise()` から呼ぶことで、
全 10 本のコールバックを一か所で配線する構造にしたんだよ：

```cpp
// 代表例
projectSerializer_.getCoreWindowBounds = [this] () -> juce::Rectangle<int> {
    return mainWindow != nullptr ? mainWindow->getBounds() : juce::Rectangle<int>{};
};
projectSerializer_.onMixerWindowRestored = [this] (bool visible, juce::Rectangle<int> bounds) {
    if (visible) { toggleMixerWindow (true); mixerWindow->setBounds (bounds); ... }
    else         { mixerWindow->setVisible (false); ... }
};
```

### ③ メンバー宣言順序による安全な参照初期化

Phase A と同じパターンで、宣言順が参照の有効性を保証するんだよ：

```cpp
juce::OwnedArray<BridgeInstance> bridges;
MidiRoutingManager               midiRouter        { bridges };
ProjectSerializer                projectSerializer_ { bridges, audioEngine, midiRouter,
                                                      deviceManager, appProperties };
```

### ④ `std::optional<MixerSettings>` で「未設定」を明示

`takePendingMixer` の戻り値を `std::optional` にすることで、
「ペンディングデータなし」と「デフォルト値のデータあり」が明確に区別できるんだよ。
呼び出し側は `has_value()` で安全にチェックできるんだよ：

```cpp
if (pendingMixer.has_value())
{
    const auto& ms = *pendingMixer;
    b->mixerGain.store (ms.gain, ...);
    // ...
}
```

---

## 変更ファイル

| ファイル | 変更内容 |
|---|---|
| `Source/Core/ProjectSerializer.h` | 新規作成 |
| `Source/Core/ProjectSerializer.cpp` | 新規作成 |
| `CMakeLists.txt` | `Source/Core/ProjectSerializer.cpp` をビルドターゲットに追加 |
| `Source/Main.cpp` | ProjectSerializer に移譲、旧メンバー変数・メソッドを削除、クロージャ注入に更新 |

---

## ビルド結果

```
Main.cpp
ProjectSerializer.cpp
LVH-PRO.vcxproj -> LVH-PRO_artefacts\Debug\LVH-PRO.exe
```

エラー・警告ゼロで通ったんだよ！

---

Phase B 完了なんだよ！
次は Phase C（`BridgeManager` — ブリッジのライフサイクル管理の抽出）に進む予定なんだよ。
引き続きよろしくなんだよ～！

しずく 🎚️
