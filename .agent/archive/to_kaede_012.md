# 報告: Multi-Process Audio Loop & Event-based Sync — 完了

## From: Shizuku
## To: Kaede

---

かえで、ミッション012完了したよ！マルチプロセスで実際に音が出たんだよっ！
Core側のレベルメーターも振れて、動作確認完了なんだよ！

## 実装内容

### 1. SharedAudioLayout の拡張

```cpp
struct SharedAudioLayout {
    float   sampleRate   = 44100.0f;
    int32_t bufferSize   = 512;
    int32_t numChannels  = 2;
    int32_t padding      = 0;
    float   audioIn [2][4096];   // Core → Bridge
    float   audioOut[2][4096];   // Bridge → Core
};
// sizeof(SharedAudioLayout) ≈ 65KB
```

### 2. SyncEvents クラス (IPCManager.h/cpp)

Windows Named Events (自動リセット) による Core↔Bridge の同期：
- `LVHSync<timestamp>_Req`  : Core → Bridge に処理依頼
- `LVHSync<timestamp>_Done` : Bridge → Core に完了通知
- `create()` / `open()` / `signalRequest()` / `waitForDone()` 等を実装

### 3. BridgeSyncProcessor (新規: BridgeSyncProcessor.h)

Core の AudioProcessorGraph に挿入する生成器ノード：
1. `audioIn` に Core の入力オーディオをコピー
2. `signalRequest()` で Bridge に処理依頼
3. `waitForDone(10ms)` で Bridge の完了を待機
4. `audioOut` から Bridge の処理済み音声を受け取り出力

※ 入力バスなし（生成器型）で AudioProcessorGraph に正しく組み込まれる

### 4. BridgeAudioThread (BridgeMain.cpp)

Bridge 側の高優先度オーディオ処理スレッド：
1. `waitForRequest(100ms)` で待機
2. `audioIn` から入力データ取得
3. `MidiMessageCollector` から IPC 経由 MIDI を取得
4. `pluginInstance->processBlock()` 呼び出し
5. 結果を `audioOut` に書き込み
6. `signalDone()` で Core に通知

### 5. MidiMessageCollector によるスレッドセーフ MIDI 橋渡し

IPC スレッド (onMidiReceived) → `midiCollector.addMessageToQueue()` → オーディオスレッド (removeNextBlockOfMessages) という正しい JUCE パターンで実装。

### 6. Core の AudioEngine 統合

`buildGraphWithBridgeSync(unique_ptr<AudioProcessor>)` メソッドを追加。Bridge 接続時に自動でグラフ切り替え、切断時に SineWave に復帰。

## 動作確認結果 (bridge_log.txt より抜粋)

```
[SyncEvents] Opened: LVHSync1773750809772
[Bridge] Sync events opened successfully.
[Bridge] BridgeAudioThread started.
[BridgeAudio] Thread started.
[BridgeAudio] First processBlock: numSamples=480 midiEvents=1
```

✅ BridgeAudioThread 起動確認
✅ processBlock が正しい numSamples=480 で呼ばれることを確認
✅ MIDI イベントが processBlock に届くことを確認
✅ **Core のレベルメーターが振れた（音声データが Core に返ってきた）**
✅ **Bridge のプラグイン音が Core スピーカーから出力された**

## 次のステップについて

かえでへの確認事項：
1. MIDI タイムスタンプの精度向上（現在は大まかなタイミング）
2. BridgeAudioThread のスレッド優先度設定（JUCE 7 の正しい API の確認）
3. 複数 Bridge インスタンスの管理
4. 独立ウィンドウ管理

— Shizuku
