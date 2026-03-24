# Mission 016 完了報告: Project Persistence & Preferences Update

## From: Shizuku
## To: Kaede

---

かえでちゃん、Mission 016 完了したんだよっ！テストもバッチリ通ったんだよ！

---

## 実装内容

### 1. プロジェクト保存・読込（`.lvh` XML）

**保存内容:**
- 各 Bridge のプラグインパス・ウィンドウ位置（X/Y/W/H）・プラグイン音色（Base64）
- MIDI ルーティング設定（All Bridges / 個別）
- オクターブオフセット・トランスポーズ・チャンネルフィルター

**UI:** LVH ロゴ右クリックメニューに「Save Project...」「Open Project...」追加

**変更ファイル:** `Main.cpp`

---

### 2. IPC 拡張（ウィンドウ位置 + プラグイン音色）

新規 IpcMessageType:
- `WindowPos (0x06)`: 双方向でウィンドウ位置を交換（Bridge→Core で報告、Core→Bridge でリストア）
- `RequestState (0x07)`: Core→Bridge、音色データ要求
- `StateData (0x08)`: Bridge→Core、`getStateInformation()` の結果を返送
- `SetState (0x09)`: Core→Bridge、`setStateInformation()` で音色をリストア

**変更ファイル:** `IPCManager.h`, `IPCManager.cpp`, `BridgeInstance.h`, `BridgeInstance.cpp`, `BridgeMain.cpp`

---

### 3. 非同期セーブ設計

プラグインが複数ある場合も安全に対応:
- 全 Bridge に RequestState 送信
- 全員の返答が揃ったら即 XML 書き込み
- **2秒タイムアウト**: 応答がなくても強制的に保存（応答済み分だけ保存）

---

### 4. Plugin Paths 設定ページ（実装済み）

- `PluginPathsPage` を `ListBox` ベースに刷新
- 「Add Path...」でフォルダ追加、「Remove」で削除
- `ApplicationProperties` に保存、変更時に自動再スキャン

**変更ファイル:** `SettingsWindow.h`, `SettingsWindow.cpp`

---

### 5. PluginScanThread — ユーザーパス対応

- デフォルトの VST3 検索パス ＋ ユーザー設定パスを両方スキャン

**変更ファイル:** `Source/Core/PluginScanThread.h`

---

## テスト結果

- Save Project... → .lvh ファイル生成 ✓
- Open Project... → Bridge 自動起動・音色復元 ✓
- ウィンドウ位置復元 ✓
- MIDI ルーティング復元 ✓
- Plugin Paths 追加・削除・再スキャン ✓
- Release ビルド成功 ✓

---

しずく：「なべさんにも褒めてもらえて、お腹いっぱいになれそうな気分なんだよっ！！次のミッションも待ってるんだよ〜！」
