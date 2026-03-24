# Mission: Multi-Process Audio Loop & Event-based Sync

## From: Kaede
## To: Shizuku

---

しずくちゃん、ミッション011大成功おめでとう！
共有メモリの基盤が最速で整ったね。これでいよいよ「マルチプロセスで実際に音を処理する」心臓部の構築に入るよ。

今回の目標は、**Coreのオーディオコールバックに同期してBridgeがオーディオ処理を行い、結果を書き戻す** 仕組みの完成だよ。

## 今回のミッション

### 1. 共有メモリ・レイアウトの拡張
`IPCManager.h` の `SharedAudioLayout` に、実際の波形データを保持する領域を追加してほしいんだ。
- `float audioIn[2][4096]` と `float audioOut[2][4096]` くらいを確保しておけば十分かな。
- 現在の `SharedMemoryBuffer::kDefaultSize` (36KB程度) でも収まると思うけど、余裕を持って調整してね。

### 2. 同期機構 (Windows Named Events) の導入
低レイテンシで音をやり取りするため、Windows の `Event` （名前付きイベント）を使って Core と Bridge の呼吸を合わせよう。
- `evtRequestProcess`: Core が Bridge に処理を依頼する合図。
- `evtProcessDone`: Bridge が処理を終えてデータを書き戻したことを Core に伝える合図。

### 3. Bridge 側のオーディオ・スレッド実装
Bridgeはメインスレッド（GUI用）とは別に、**高優先度なオーディオ処理スレッド** を用意する必要があるよ。
- 定義: `BridgeAudioThread` (juce::Thread)
- 処理ループ:
  1. `evtRequestProcess` を待つ (Wait)。
  2. 共有メモリから入力データと MIDI を取得。
  3. `pluginInstance->processBlock` を呼び出す。
  4. 結果を共有メモリの `audioOut` 領域に書き込む。
  5. `evtProcessDone` をセット (Signal)。

### 4. Core 側の AudioEngine 統合
- `AudioEngine::audioDeviceIOCallback` にて：
  1. 入力バッファの内容を共有メモリにコピー。
  2. `evtRequestProcess` をセット。
  3. `evtProcessDone` を待つ (タイムアウト 10ms 程度)。
  4. 共有メモリから処理済みデータを受け取って出力に充てる。

---
なべさんには、この指示書を確認してもらった後で作業をお願いしてね。
かえで： 「音が出た瞬間の感動は、格別だよ！しずくちゃんなら絶対にできる。楽しみにしてるね！」
