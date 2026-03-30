# 報告書 035-A: MIDI & オーディオ安定性の根本修正

## From: Shizuku
## To: Kaede

---

かえでちゃん、Mission 035-A の調査・修正が完了したんだよっ！✨
当初の「Bridge クラッシュ調査」というテーマだったけど、掘り下げていくとクラッシュよりもっと根深い MIDI / オーディオの根本的な問題が複数見つかったんだよ。全部修正して、開発PC・ホビーPCの両方で動作確認できたんだよ！

---

## ✅ 実施内容

### Fix 1 — Bridge IPC の冗長な MIDI ログを削除

**ファイル**: `Source/IPCManager.cpp`

**問題**
Bridge プロセスが MIDI メッセージを受信するたびに、メッセージスレッド上でログ書き込みを行っていた。
演奏中（特に速いパッセージやコード演奏）は毎ブロックで大量のログが発生し、メッセージスレッドのスループットを圧迫していた。

**修正**
`BridgeIpcClient::messageReceived` 内の per-message MIDI ログを削除。
初回受信時のみ診断ログを出力する方式に変更（`BridgeMain.cpp` の `firstMidiReceived` フラグで制御）。

---

### Fix 2 — ハードウェア MIDI の二重送信を修正

**ファイル**: `Source/Main.cpp`

**問題**
`MidiKeyboardState` のコールバック (`handleNoteOn` / `handleNoteOff`) と、`handleIncomingMidiMessage`（ハードウェア MIDI 入力スレッド）の両方が `midiRouter.sendMidi` を呼び出していた。
ハードウェアコントローラで1音弾くと、Bridge には同じ Note-On が2回送信されていた。

**修正**
`std::atomic<bool> fromHardwareMidi_` フラグを導入。
`handleIncomingMidiMessage` 呼び出し中のみ `true` にセットし、`handleNoteOn/Off` でハードウェア由来の場合は `midiRouter.sendMidi` をスキップ。

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
Bridge 側の読み取りが遅い場合、パイプ書き込みがブロックし、UI フリーズや MIDI タイミング遅延が発生しうる構造だった。

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
`BridgeInstance::state` が通常の `enum` メンバー変数だったため、リリースビルド（最適化あり）において MIDI 入力スレッドからの読み取り時に値がキャッシュされ、`Idle` のまま変化しなかった。
結果、`sendMidi` が常に `false` を返し、ハードウェア MIDI が Bridge に届かない状態になっていた。

**修正**
`state` を `std::atomic<State>` に変更。書き込みは `memory_order_release`、読み取りは `memory_order_acquire` を使用。
合わせて `midiRouter.sendMidi` の呼び出しを `MessageManager::callAsync` 経由に移動し、メッセージスレッドで状態の書き込みと読み取りが同一スレッドで行われることを保証。

> **備考**: Debug ビルドでは問題が再現しなかったため、発見が困難だった典型的なリリースビルドのみの不具合なんだよ。

---

### Fix 5 — ドラム音源・サンプラーが完全に無音になる問題を修正

**ファイル**: `Source/BridgeMain.cpp`

**問題**
ドラム音源や一部のサンプラーは、デフォルトで 16 Stereo（32ch）など多数のバス出力を持つ VST3 プラグインである。
Bridge の SharedMemory は Stereo（2ch）固定のため、プラグインが 32ch で初期化されると出力バッファとのレイアウトが一致せず、音声が完全にゼロになっていた。

**修正前ログ**:
```
[Bridge] Bus layout before prepare: in=0 out=16 out[0]=Stereo ... out[15]=Stereo
```

**修正**
`forceStereoLayout()` 関数を新設し、`prepareToPlay` 前に呼び出す。
- Step 1: バス0のみ Stereo に設定し、残りを Disabled に変更を試みる。
- Step 2: 失敗した場合（プラグインがバス削除を拒否）、全バスを Stereo のまま維持し、`getTotalNumOutputChannels()` で出力チャンネル数を動的取得して正しく処理。

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
リアルタイムオーディオの大原則（オーディオスレッドでのヒープ確保禁止）に違反しており、サンプラー系音源での顕著なノイズ・音切れの原因となっていた。

**修正**
`audioBuffer` を初回ブロック時のみ確保し、以降は同じバッファをクリアしながら再利用する方式に変更。

---

### Fix 7 — MIDI タイムスタンプ統一

**問題**
Core 側で `MidiMessage` を作成した時点でタイムスタンプが `0.0` のままだった。
Bridge 側の `MidiMessageCollector` はタイムスタンプが `0.0` のメッセージをエラーとして除外する。

**修正**
Core の `handleIncomingMidiMessage` 内、および Bridge の `onMidiReceived` コールバック内の両方で付与：

```cpp
msg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
```

---

## ✅ 変更ファイルまとめ

| ファイル | 変更内容 |
|---------|---------|
| `Source/Main.cpp` | `fromHardwareMidi_` フラグ導入、`callAsync` 経由ルーティング、タイムスタンプ付与 |
| `Source/BridgeInstance.h` | `state` を `std::atomic<State>` に変更、`MidiSenderThread` 前方宣言追加 |
| `Source/BridgeInstance.cpp` | `MidiSenderThread` 実装、`sendMidi` をキューベースに変更、`launch/shutdown` 更新 |
| `Source/BridgeMain.cpp` | `forceStereoLayout()` 追加、`preparePlugin()` 更新、バッファ再利用化、タイムスタンプ付与、`createPluginInstance` を 48kHz/480 samples に変更 |
| `Source/IPCManager.cpp` | per-message MIDI ログ削除 |

---

## ✅ 動作確認結果

| テスト項目 | 開発PC | ホビーPC |
|-----------|--------|---------|
| PC キーボードでシンせ演奏 | ✅ 正常 | ✅ 正常 |
| ハードウェア MIDI コントローラでシンせ演奏 | ✅ 正常 | ✅ 正常 |
| Grand Piano (VST3 Sampler) 音声出力 | ✅ 正常 | ✅ 正常 |
| Upright Piano (VST3 Sampler) 音声出力 | ✅ 正常 | ✅ 正常 |
| RF-Drums (VST3 ドラム音源) 音声出力 | ✅ 正常 | ✅ 正常 |
| 複数プラグイン同時起動 | ✅ 正常 | ✅ 正常 |
| MIDI の二重送信なし（ログで確認） | ✅ 正常 | ✅ 正常 |

---

## 📝 かえでへの申し送り事項

- **Fix 4（state アトミック化）** は Debug では再現しない Release 専用の不具合だったんだよ。今後も `BridgeInstance` の内部状態を複数スレッドから参照する場合は `std::atomic` を徹底してほしいんだよ！
- **Fix 5（バス強制縮小）** はシンせとドラム音源で VST3 のデフォルトバス数が大きく異なることが判明したんだよ。将来的にマルチアウト対応を検討するときは SharedMemory のレイアウト拡張も一緒に設計してほしいんだよ！
- `report_035.md`（プロジェクトルート）に詳細な技術レポートも残してあるんだよ。

---

035-A、全部片付いたんだよっ！🍪✨
次は B → C → D → E と進んでいくんだよ！かえでちゃん、次の指示もよろしくなんだよ！
