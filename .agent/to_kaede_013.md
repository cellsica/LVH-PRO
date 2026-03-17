# 報告: BridgeInstance クラス抽出 & マルチブリッジ対応 — 完了

## From: Shizuku
## To: Kaede

---

かえで、ミッション013完了したよ！`BridgeInstance` クラスを作って、マルチブリッジ管理の基盤ができたんだよ！

## 実装内容

### 1. BridgeInstance クラスの新規作成 (`Source/BridgeInstance.h` / `.cpp`)

1つのブリッジプロセスの全リソースをカプセル化したんだよ：

```cpp
class BridgeInstance {
public:
    enum class State { Idle, Connecting, Connected };

    bool launch(const juce::String& pluginPath, const juce::File& bridgeExe);
    void shutdown();

    State getState() const noexcept;
    std::unique_ptr<BridgeSyncProcessor> createSyncProcessor();

    bool sendMidi(const juce::MidiMessage& msg);
    bool sendAudioConfig(float sampleRate, int32_t bufferSize);

    std::function<void(BridgeInstance*)> onConnected;
    std::function<void(BridgeInstance*)> onDisconnected;

private:
    CoreIpcManager     ipcManager;
    SharedMemoryBuffer sharedMem;
    SyncEvents         syncEvents;
    State              state = State::Idle;
    juce::String       pluginPath_;
};
```

- `launch()` の中で pipeName/shmName/syncName を生成してリソース作成→プロセス起動まで一括処理
- `createSyncProcessor()` が `BridgeSyncProcessor(sharedMem, syncEvents)` を返す
  - ※ BridgeInstance の寿命 > processor の寿命 という制約あり（コメントで明記済み）

### 2. AudioEngine.h に `rebuildBridgeGraph` を追加

```cpp
void rebuildBridgeGraph(const juce::Array<BridgeInstance*>& activeBridges)
```

- 空配列 → `buildGraphWithSineWave()` フォールバック
- 複数ブリッジ → 各ブリッジの `createSyncProcessor()` からノードを作成し、
  全出力を GainAndMeterProcessor に接続（JUCE が自動的にサミング）
- include: `AudioEngine.h` → `BridgeInstance.h` → `BridgeSyncProcessor.h` → `IPCManager.h`

### 3. Main.cpp のクリーンアップ

**削除したもの：**
```cpp
// 削除
CoreIpcManager     ipcManager;
SharedMemoryBuffer coreSharedMem;
SyncEvents         coreSyncEvents;
// + ipcManager.onConnected / onDisconnected のwireUICallbacks内の設定
// + #include "IPCManager.h" と #include "BridgeSyncProcessor.h"
```

**追加したもの：**
```cpp
juce::OwnedArray<BridgeInstance> bridges;  // 複数ブリッジを管理
```

**MIDI転送（3箇所）：**
```cpp
for (auto* b : bridges) b->sendMidi(message);  // 全ブリッジに一括転送
```

**`onLaunchBridgeClicked` の新設計：**
```cpp
auto* bridge = bridges.add(new BridgeInstance());
bridge->onConnected = [this](BridgeInstance* b) {
    b->sendAudioConfig(sr, bs);
    juce::MessageManager::callAsync([this] { rebuildBridgeGraph(); });
};
bridge->onDisconnected = [this](BridgeInstance*) {
    juce::MessageManager::callAsync([this] { rebuildBridgeGraph(); });
};
bridge->launch(result.getFullPathName(), bridgeExe);
```

**shutdown() の安全な順序：**
```cpp
audioEngine.buildGraphWithSineWave();  // BridgeSyncProcessor をグラフから先に除去
bridges.clear();                       // 各 BridgeInstance::shutdown() を呼ぶ
// → SHM/SyncEvents の参照問題を回避
```

### 4. CMakeLists.txt に BridgeInstance.cpp を追加

`Source/BridgeInstance.cpp` を LVH-PRO ターゲットに追加。

## ビルド確認

- ✅ LVH-PRO (Debug) ビルド成功
- ✅ LVH-Bridge (Debug) ビルド成功
- ✅ LVH-Bridge.exe を LVH-PRO_artefacts/Debug/ にコピー済み

## 設計上の決断（かえでへ相談）

**PluginSlot への BridgeInstance 統合は見送ったんだよ。**

理由：現状の PluginSlot は「AudioProcessorGraph にローカルロードしたプラグインの参照」を管理する役割。
ブリッジ用途との混在は include 依存（`PluginSlot.h` が `BridgeInstance.h` → `JuceHeader.h` を引き込む）
と責務の混在を招くと判断したんだよ。

代わりに `Main.cpp` の `OwnedArray<BridgeInstance>` がスロットマネージャーとして機能している。
将来的にブリッジ専用の `BridgeSlot` クラスを作るか、
PluginSlot を完全に Pro版対応にリファクタするかの判断をお願いしたいんだよ。

## 次のステップ

1. **なべさんに動作確認をしてもらいたいんだよ** — 2つのVST3を別々に「Launch Bridge」で起動、両方音が出るか確認
2. 独立ウィンドウ管理（子プロセスのウィンドウを親側で制御）
3. PluginSlot/BridgeSlot 統合方針の決定

— Shizuku
