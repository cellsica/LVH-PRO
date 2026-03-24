# 報告: Audio Sync & Shared Memory Foundation — 完了

## From: Shizuku
## To: Kaede

---

かえで、ミッション011完了したよ！全項目クリアなんだよっ！

## 実装内容

### 1. AudioConfig (SR/BS) の同期

**Core側 (Main.cpp)**:
- `ipcManager.onConnected` コールバックに、接続時に `deviceManager.getAudioDeviceSetup()` の SR/BS を `sendAudioConfig()` で送信する処理を追加した。

**Bridge側 (BridgeMain.cpp)**:
- `onAudioConfigReceived` コールバックで `mainWindow->preparePlugin(sr, bs)` を呼び出し。
- `MainWindow::preparePlugin()` を新規追加。`pluginInstance->prepareToPlay()` を呼び出してログ出力。

**IPCManager.cpp**:
- `BridgeIpcClient::messageReceived` の AudioConfig ケースに `onAudioConfigReceived` コールバックを追加した。

### 2. 共有メモリ基盤 (SharedMemoryBuffer)

**IPCManager.h/cpp に新規クラス `SharedMemoryBuffer` を実装**:
- Windows `CreateFileMapping` / `MapViewOfFile` を使用
- Core側: `create(name, size)` で名前付きファイルマッピングを作成
- Bridge側: `open(name)` で既存のマッピングを開く
- サイズ: `sizeof(SharedAudioLayout) + 2ch * 4096samples * 4bytes` = 約36KB (固定)
- 既存の `SharedAudioLayout` 構造体のヘッダーとして先頭に配置

**起動フロー**:
1. Core が `LVHAudio<timestamp>` の名前で共有メモリを作成
2. Bridge を `--shm-name LVHAudio<timestamp>` 引数付きで起動
3. Bridge が `open()` で接続

## 動作確認結果 (bridge_log.txt より抜粋)

```
Command Line: --plugin "...monofury.vst3" --ipc-pipe LVH-Bridge-xxx --shm-name LVHAudio1773749689473
[SharedMem] Opened: LVHAudio1773749689473 (36864 bytes)
[Bridge] Shared memory opened successfully.
[Bridge IPC] AudioConfig received: SR=48000.0 BS=480
[Bridge] prepareToPlay called: SR=48000.0 BS=480
```

✅ 共有メモリの作成・オープン成功
✅ AudioConfig (SR=48000Hz, BS=480) の同期確認
✅ `prepareToPlay` 呼び出し確認
✅ MIDI転送も引き続き正常動作

## コミット

`84614fb` — feat: AudioConfig sync, SharedMemoryBuffer foundation, and CLAUDE.md

## 次のステップについて

共有メモリの「読み書き」と同期機構（ロックフリーリングバッファ or セマフォ）の設計をかえでに確認したいんだよ。
Bridge が処理した音声データを Core に返す部分の設計方針を教えてほしいんだよ！

— Shizuku
