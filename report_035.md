# LVH-PRO 開発報告書 035
**対象ミッション**: Mission 035-A — MIDI & オーディオ安定性の根本修正
**実施日**: 2026-03-26
**担当**: しずく (Claude / Coding Expert)
**確認者**: なべ

---

## 概要

本報告書は、Mission 035-A において実施した MIDI 関連バグの根本修正、および副次的に発見・解決したオーディオ無音問題の修正内容をまとめたものです。
今回の修正はライブ演奏における信頼性に直結する重要な変更であり、リリースノートへの記載を推奨します。

---

## 修正内容一覧

### Fix 1 — Bridge IPC の冗長な MIDI ログを削除

**ファイル**: `Source/IPCManager.cpp`

**問題**
Bridge プロセスが MIDI メッセージを受信するたびに、メッセージスレッド上でログ書き込みを行っていた。
演奏中（特に速いパッセージやコード演奏）は毎ブロックで大量のログが発生し、メッセージスレッドのスループットを圧迫していた。

**修正**
`BridgeIpcClient::messageReceived` 内の per-message MIDI ログを削除。
初回受信時のみ診断ログを出力する方式に変更（Bridge 側 `BridgeMain.cpp` の `firstMidiReceived` フラグで制御）。

---

### Fix 2 — ハードウェア MIDI の二重送信を修正

**ファイル**: `Source/Main.cpp`

**問題**
`MidiKeyboardState` のコールバック (`handleNoteOn` / `handleNoteOff`) と、`handleIncomingMidiMessage`（ハードウェア MIDI 入力スレッド）の両方が `midiRouter.sendMidi` を呼び出していた。
その結果、ハードウェアコントローラで1音弾くと、Bridge には同じ Note-On が2回送信されていた。

**修正**
`std::atomic<bool> fromHardwareMidi_` フラグを導入。
`handleIncomingMidiMessage` 呼び出し中のみ `true` にセットし、`handleNoteOn/Off` 内でこのフラグを参照してハードウェア由来の場合は `midiRouter.sendMidi` をスキップ。

```
[Hardware MIDI] → handleIncomingMidiMessage
    ├─ fromHardwareMidi_ = true
    ├─ keyboardState.processNextMidiEvent() → handleNoteOn()
    │       ↑ fromHw = true なので midiRouter.sendMidi をスキップ ✓
    └─ callAsync → midiRouter.sendMidi (1回のみ送信) ✓
```

---

### Fix 3 — BridgeInstance::sendMidi をメッセージスレッドブロッキングから解放

**ファイル**: `Source/BridgeInstance.h`, `Source/BridgeInstance.cpp`

**問題**
Named Pipe への書き込み (`ipcManager.sendMidi`) が JUCE メッセージスレッド上で同期的に実行されていた。
Bridge 側の読み取りが遅い場合、パイプ書き込みがブロックし、UI のフリーズや MIDI タイミングの遅延が発生しうる構造だった。

**修正**
`MidiSenderThread`（`BridgeInstance` のネストクラス）を新設。
`sendMidi` は MIDI メッセージをスレッドセーフなキューに積むだけ（ノンブロッキング）で即時返却。
実際の pipe 書き込みは専用スレッドが非同期で行う。

```
[メッセージスレッド]  sendMidi() → enqueue() → return (即時) ✓
[MidiSenderThread]   queue をスワップ → ipcManager.sendMidi (ブロック可) ✓
```

---

### Fix 4 — BridgeInstance::state をアトミック変数に変更（リリースビルド対応）

**ファイル**: `Source/BridgeInstance.h`, `Source/BridgeInstance.cpp`

**問題**
`BridgeInstance::state` が通常の `enum` メンバー変数だったため、リリースビルド（最適化あり）において MIDI 入力スレッドからの読み取り時に値がキャッシュされ、`Idle` のまま変化しないことがあった。
結果、`sendMidi` が常に `false` を返し、ハードウェア MIDI が Bridge に届かない状態になっていた。

**修正**
`state` を `std::atomic<State>` に変更。書き込みは `memory_order_release`、読み取りは `memory_order_acquire` を使用。
合わせて `midiRouter.sendMidi` の呼び出しを `MessageManager::callAsync` 経由に移動し、メッセージスレッドで状態の書き込みと読み取りが同一スレッドで行われることを保証。

> **背景**: Debug ビルドでは問題が再現しなかったため、発見が困難だった典型的なリリースビルドのみの不具合。

---

### Fix 5 — ドラム音源・サンプラーが完全に無音になる問題を修正

**ファイル**: `Source/BridgeMain.cpp`

**問題**
ドラム音源や一部のサンプラーは、デフォルトで 16 Stereo（32ch）など多数のバス出力を持つ VST3 プラグインである。
Bridge の SharedMemory は Stereo（2ch）固定のため、プラグインが 32ch で初期化されると出力バッファと SharedMemory のレイアウトが一致せず、音声が完全にゼロになっていた。

**原因ログ（修正前）**:
```
[Bridge] Bus layout before prepare: in=0 out=16 out[0]=Stereo ... out[15]=Stereo
```

**修正**
`forceStereoLayout()` 関数を新設し、`prepareToPlay` 前に呼び出す。
- Step 1: バス0のみ Stereo に設定し、残りを Disabled に変更を試みる。
- Step 2: 失敗した場合（プラグインがバス削除を拒否）、全バスを Stereo のまま維持し、processBlock で出力チャンネル数を `getTotalNumOutputChannels()` で動的取得して正しく処理。

**修正後ログ**:
```
[Bridge] Bus layout forced to stereo output.    ← 成功ケース (Grand Piano, Upright Piano)
[Bridge] Bus layout: all outputs set to stereo. ← フォールバック (RF-Drums)
[BridgeAudio] First processBlock: numSamples=480 pluginOutCh=2 midiEvents=0
```

---

### Fix 6 — オーディオスレッドでの毎ブロック・ヒープアロケーションを排除

**ファイル**: `Source/BridgeMain.cpp` (`BridgeAudioThread::run`)

**問題**
`juce::AudioBuffer<float>` がオーディオスレッドの `processBlock` 呼び出しごとに生成・破棄されていた。
これはリアルタイムオーディオの大原則（オーディオスレッドでのヒープ確保禁止）に違反しており、特にサンプラー系音源で顕著なノイズ・音切れの原因となっていた。

**修正**
`audioBuffer` を初回ブロック時のみ確保し、以降は同じバッファをクリアしながら再利用する方式に変更。
`bufChannels` も初回のみ算出（`getTotalNumOutputChannels()` の結果をキャッシュ）。

---

## MIDI タイムスタンプ統一

**修正前の状況**
- Core 側 `handleIncomingMidiMessage` で `MidiMessage` を作成した時点でタイムスタンプが `0.0` のままだった。
- Bridge 側の `MidiMessageCollector` はタイムスタンプが `0.0` のメッセージをエラーとして除外する。

**修正**
Core → `handleIncomingMidiMessage` 内、および Bridge → `onMidiReceived` コールバック内の両方で、メッセージ生成時に以下でタイムスタンプを付与：

```cpp
msg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
```

---

## 動作確認結果

| テスト項目 | 開発PC | ホビーPC |
|-----------|--------|---------|
| PC キーボードでシンセ演奏 | ✅ 正常 | ✅ 正常 |
| ハードウェア MIDI コントローラでシンせ演奏 | ✅ 正常 | ✅ 正常 |
| Grand Piano (VST3 Sampler) 音声出力 | ✅ 正常 | ✅ 正常 |
| Upright Piano (VST3 Sampler) 音声出力 | ✅ 正常 | ✅ 正常 |
| RF-Drums (VST3 ドラム音源) 音声出力 | ✅ 正常 | ✅ 正常 |
| 複数プラグイン同時起動 | ✅ 正常 | ✅ 正常 |
| MIDI の二重送信なし（ログで確認） | ✅ 正常 | ✅ 正常 |

---

## 変更ファイルまとめ

| ファイル | 変更内容 |
|---------|---------|
| `Source/Main.cpp` | `fromHardwareMidi_` フラグ導入、`callAsync` 経由のルーティング、タイムスタンプ付与 |
| `Source/BridgeInstance.h` | `state` を `std::atomic<State>` に変更、`MidiSenderThread` 前方宣言追加 |
| `Source/BridgeInstance.cpp` | `MidiSenderThread` 実装、`sendMidi` をキューベースに変更、`launch/shutdown` 更新 |
| `Source/BridgeMain.cpp` | `forceStereoLayout()` 追加、`preparePlugin()` 更新、`BridgeAudioThread` のバッファ再利用化、タイムスタンプ付与、`createPluginInstance` を 48kHz/480 samples に変更 |
| `Source/IPCManager.cpp` | per-message MIDI ログ削除 |

---

## 所感・補足

今回の修正群は、それぞれ独立した問題でありながら、組み合わさって「MIDI が届かない」「音が出ない」という症状として現れていた。
特に **Fix 4（state のアトミック化）** は Debug ビルドでは一切再現せず、Release ビルドのみで発生する「コンパイラ最適化による変数のキャッシュ」という厄介な問題であり、根本原因の特定に時間を要した。

また **Fix 5（バス強制縮小）** は、VST3 プラグインの種類ごとに動作が異なる（シンセはデフォルトで Stereo、ドラム音源は 16 Stereo）という仕様を踏まえた修正で、ドラム音源対応の核心部分となっている。

以上の修正により、LVH-PRO はハードウェア MIDI コントローラによる演奏と、多様な VST3 プラグイン（シンセ/サンプラー/ドラム音源）の使用において、安定した動作を実現できるようになった。
