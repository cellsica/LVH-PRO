# 報告: IPC 切断検知バグ修正 + UI リデザイン + MIDI ルーティング — 完了

## From: Shizuku
## To: Kaede

---

かえで、ミッション014（バグ修正フェーズ）に加えて、UI リデザインと MIDI ルーティング機能も完了したよ！
なべさんからの追加要望も全部実装してテスト確認まで取れたんだよっ！

---

## Part 1: IPC 切断検知バグ修正 (Bug①②③)

### 修正対象バグ

| バグ | 症状 |
|------|------|
| Bug① | Bridge#1 を閉じる → Bridge#2 を開く → Bridge#2 から音が出ない |
| Bug③ | Bridge#2 無音状態で Core の仮想キーボードを押す → Core がフリーズ |

両バグの共通根本原因は **`CoreIpcManager::ListenThread::run()` の `createPipe(pipeName, -1)`** だったんだよ。

### 根本原因の詳細分析

#### JUCE NamedPipe サーバーの切断後挙動

Bridge が切断すると、JUCE の NamedPipe サーバー（ConnectionThread）は内部で再接続ループに入る：

```
read() → ERROR_BROKEN_PIPE
→ disconnectPipe(connected=false)
→ while (ConnectNamedPipe(INFINITE)) ← 永遠にブロック
```

`pipeReceiveMessageTimeout = -1 (INFINITE)` のため：
- `connectionLostInt()` は発火しない（`read` が -1 を返さない）
- `isConnected()` は true のまま（ConnectionThread が生存中）
- Bridge が再接続しない限り状態がリセットされない

#### Bug① のメカニズム

1. Bridge#1 切断 → ConnectionThread が reconnect ループでブロック
2. `connectionLostInt` 未発火 → `state = Connected` のまま、`bridges` に b1 が残る
3. Bridge#2 起動 → `onConnected` → `rebuildBridgeGraph`
4. b1（Connected）+ b2（Connected）の両方を active と判定
5. `processBlock` が b1 に `signalRequest` → `waitForDone(200ms)` タイムアウト
6. **1ブロック 200ms かかる → オーディオコールバック超過 → 音が出ない（無音）**

#### Bug③ のメカニズム

1. Bridge#1 切断 → 同様に `state = Connected`、`isConnected() = true` のまま
2. ユーザーがキーボード押下 → `callAsync(sendMidi)` → メッセージスレッドで実行
3. `sendMidi` の各ガードを通過（state は Connected、threadIsRunning は true）
4. `pipe->write(-1)` → `ConnectNamedPipe(INFINITE)` → **メッセージスレッドが永久フリーズ**

### 修正内容

#### 修正① `IPCManager.cpp` — タイムアウト変更

```cpp
// Before（バグあり）:
bool ok = owner.createPipe(pipeName, -1 /* no receive timeout */);

// After（修正）:
bool ok = owner.createPipe(pipeName, 5000 /* 5s timeout */);
```

`pipeReceiveMessageTimeout = 5000ms` にすることで、Bridge 切断後 5 秒以内に
`ConnectNamedPipe` がタイムアウト → `read()` が -1 を返す →
`connectionLostInt()` → `connectionLost()` → `onDisconnected` が正常発火。

#### 修正② ハートビート機構の追加 — 偽の切断を防止

`pipeReceiveMessageTimeout = 5000ms` は **データ読み取りにも適用される**。
Bridge は Core にデータを送らないため、`read(5000)` が 5 秒ごとにタイムアウト →
偽の `connectionLost` → サイン波に戻る問題が発生した。

**解決策：Bridge が 1.5 秒ごとに軽量な Heartbeat メッセージを Core に送信する。**

| 状態 | 挙動 |
|------|------|
| Bridge 生存中 | 1.5 秒ごとに Heartbeat → Core の `read(5000)` がデータを受け取り続ける → 切断しない |
| Bridge が本当に切断 | Heartbeat が止まる → 5 秒後にタイムアウト → `connectionLostInt` が正常発火 |

#### 修正③ `BridgeMain.cpp` — IPC 接続順序の変更

Debug ビルドでは VST3 ロードに 5 秒以上かかるため、IPC 接続を VST3 ロードより先に開始する。

```cpp
// After（修正）:
ipcClient->connectAsync(ipcPipeName, 10000);  // バックグラウンドで接続開始
mainWindow.reset(new MainWindow(...));         // VST3ロードと並行して接続が進む
```

### テスト結果（Debug ビルド）

- ✅ Bug① 修正確認：Bridge#1 閉じる → Bridge#2 開く → Bridge#2 から音が出る
- ✅ Bug③ 修正確認：Bridge#2 無音状態で Core キーボード → フリーズしない
- ✅ 通常動作確認：Bridge#1 起動 → 弾くと音が出る

---

## Part 2: Core UI リデザイン

### 変更内容

従来のプラグインビューポート中心のレイアウトを廃止し、以下の構成に刷新：

```
Toolbar (44px 固定)
───────────────────────── HResizer（上）← SystemLog の高さが変わる
SystemLog                 ← サイズ変更の吸収役（常に残り全部）
───────────────────────── HResizer（上）← MIDILog の高さが変わる
InfoMonitorPanel (MIDILog)
───────────────────────── HResizer（下）← Keyboard の高さが変わる
PcKeyboardComponent (仮想キーボード)
```

### HResizer の仕様

- **上スプリッター**: ドラッグで `lastMonitorHeight` を変更、キーボードサイズは不変。SystemLog が吸収。
- **下スプリッター**: ドラッグで `lastKeyboardHeight` を変更、MIDILog サイズは不変。SystemLog が吸収。
- `StretchableLayoutManager` を廃止し、手動 `setBounds` に切り替え。

### 新規ファイル

- `Source/SystemLogPanel.h / .cpp`: タイムスタンプ付きシステムログ表示（最大500行）

---

## Part 3: Select Instruments ▶ サブメニュー

### 変更内容

ロゴ右クリックメニューに **Select Instruments ▶** サブメニューを追加。

- `KnownPluginList` のキャッシュからプラグイン一覧を表示
- キャッシュ場所: `%APPDATA%\cellsica\LVH-PRO\KnownPlugins.xml`
- リスト末尾に **Refresh Plugin List...** で再スキャン可能
- バグ修正: `existsAsFile()` → `exists()` に変更（VST3 バンドルはディレクトリのため）

---

## Part 4: MIDI Route ▶ サブメニュー

### 変更内容

ロゴ右クリックメニューの MIDI Input の下に **MIDI Route ▶** サブメニューを追加。

| 選択肢 | 動作 |
|--------|------|
| All Bridges（デフォルト） | 全 Bridge に MIDI 送信 |
| 個別 Bridge 名 | 選択した Bridge のみに送信 |

### 実装設計

```cpp
std::atomic<bool>            routeToAll       { true };
std::atomic<BridgeInstance*> midiTargetBridge { nullptr };
```

- `std::atomic` を使用 → MIDI 入力スレッド / メッセージスレッド 両方から安全にアクセス
- 追加ポーリングなし → **レイテンシーへの影響ゼロ**

### 安全策

- Bridge が切断された場合、`onDisconnected` 内で自動的に **All Bridges モードへリセット**
- SystemLog に「MIDI Route reset to: All Bridges」を表示
- `BridgeInstance::sendMidi()` の既存 2 重ガード（`state` + `isConnected()`）がフォールバックとして機能

---

## ビルド状況

- ✅ Debug ビルド成功（LVH-PRO + LVH-Bridge）
- ✅ Release ビルド成功（LVH-PRO + LVH-Bridge）
- ✅ LVH-Bridge.exe を LVH-PRO_artefacts/Debug/ および Release/ にコピー済み

---

## 次ミッション候補 (Mission 015)

### 要望①: PCキーボード オクターブずれ修正＆オクターブ切り替え機能

**現象**: Core 仮想キーボードで Z=C4、`<`=C5 という表示になっているが、実際には Z=C3、`<`=C4 が発音される（1 オクターブ低い）。

**要望**:
1. 表示と発音のオクターブを一致させる（Z で C4 が鳴るよう修正）
2. オクターブ上げ/下げ機能の追加（仮想キーボードの音域をリアルタイムに変更）

— Shizuku
