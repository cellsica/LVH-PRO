# Mission: Audio Sync & Shared Memory Foundation

## From: Kaede
## To: Shizuku

---

しずくちゃん、MIDI転送の実装お疲れ様！
次は「音を鳴らすための設定同期」と「音を戻すための道作り」をお願いするね。

## 今回のミッション

### 1. AudioConfig (SR/BS) の同期
Core (LVH-PRO) のオーディオ設定が変わった時に、Bridge側にもそれを伝えるようにしてほしいんだ。

- **Core側 (`Main.cpp`)**: 
  - `AudioEngine` が初期化（`audioDeviceAboutToStart` 等）されたタイミングや、設定変更があった時に、`ipcManager.sendAudioConfig(sampleRate, bufferSize)` を呼び出す。
- **Bridge側 (`BridgeMain.cpp`)**: 
  - `IpcMessageType::AudioConfig` を受信した際に、現在ロードされている `pluginInstance` に対して `prepareToPlay(sampleRate, bufferSize)` を呼び出すように変更して。
  - ログに「AudioConfig received: SR=xxx, BS=yyy」が出るようにしておいてね。

### 2. 音声データ返却（共有メモリ）の準備
Bridgeで鳴らした音をCoreに戻すために、共有メモリ（Shared Memory）の基礎を作ろう。

- **新規クラス/構造体の検討**: 
  - `IPCManager.h` の `SharedAudioLayout` を使って、実際に OS の共有メモリ（Windows なら `CreateFileMapping` / `MapViewOfFile`、JUCE なら `juce::MemoryMappedFile` やアドオンの共有メモリクラス）を確保する仕組みを考えてみて。
  - まずは「共有メモリの名前を生成してBridgeに渡す」ところから始めるといいかも。

### 3. ビルドと動作確認
- MIDIを弾いた時に、Bridge側のログで「正しいサンプルレートとバッファサイズで prepareToPlay が呼ばれたこと」を確認してね。

---
なべ（ユーザー）がこの指示書を確認してから作業に入ってね。
かえで： 「しずくちゃん、マルチプロセスの心臓部、よろしく頼むよ！」
