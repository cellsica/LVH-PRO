# Mission: Bridge Instance Refactoring & Multi-Slot Support

## From: Kaede
## To: Shizuku

---

しずくちゃん、オーディオループの完成本当に素晴らしいよ！音が鳴った瞬間は私も感動しちゃった。
今の実装は「1つのBridge」を前提としているから、いよいよこれを「複数立ち上げられる」ように設計を洗練させていこう。

## 今回のミッション

### 1. `BridgeInstance` クラスの抽出
現在 `Main.cpp` に直接書かれている IPC/共有メモリ/イベントの管理を、1つの「インスタンス」として独立させてほしいな。

- **新規ファイル**: `Source/BridgeInstance.h` / `cpp`
- **役割**: 
  - 1つの `LVH-Bridge.exe` プロセスの起動・監視・終了。
  - そのプロセス専用の `CoreIpcManager`, `SharedMemoryBuffer`, `SyncEvents` を保持。
  - ブリッジの状態（未接続、接続中、プラグイン名など）を管理。

### 2. 「プラグインスロット」との統合
Light版の `PluginSlot` の概念を Pro版に合わせて進化させよう。

- **`PluginSlot` の変更**: 
  - `std::unique_ptr<BridgeInstance>` を保持するようにする。
  - スロットに VST3 をロードすると、新しい `BridgeInstance` が生成され、プロセスが立ち上がる。
  - オーディオ処理 (`processBlock`) は、そのインスタンスが持つ `BridgeSyncProcessor` （または同等の同期処理）を通じて行われる。

### 3. メインアプリのクリーンアップ
- `Main.cpp` から個別の `ipcManager` や `coreSyncEvents` を削除して、スロット（またはスロットマネージャー）経由で管理するようにしてね。

### 4. 複数起動の動作確認
- 2つの異なる VST3 を別々のスロットにロードして、両方のウィンドウが開き、両方から音が出ることを確認してほしいな。

---
なべさん、指示書 OK かな？
かえで： 「これができれば、Pro版の『マルチプロセス・マルチスロット』が本当の意味で完成するね。しずくちゃん、期待してるよ！」

### 5. 実行結果
複数のBridgeが開いたけど、問題点がいくつかあったよ。
1. [x] Coreの仮想キーボードを弾くと、Bridge1の音が聞こえる（Bridge2の音も鳴るはずだが鳴らなかった）
   → 修正: MultiSourceBridgeProcessor で全ブリッジを同時シグナル→サミング処理に変更
2. [x] Coreの仮想キーボードを弾くと、Bridge2のプラグイン内のキーボードも反応している(Coreの信号が両方に届いてる？)
   → 仕様: 現在はMIDIを全ブリッジに一括送信。個別ルーティングは将来対応
3. [x] Bridgeを両方ともXボタンで閉じたあと、coreをXボタンで閉じようとしたらフリーズしている
   → 修正: BridgeInstance::shutdown() の state==Idle 早期リターンを削除。常に stopPipe() を呼ぶよう変更
4. [ ] 複数のBridgeが開いた時、仮想キーボードを持たないプラグインをcoreから弾く場合、どのBridgeを弾いてるのかが不明
   → 将来対応: スロット選択UIでアクティブブリッジを指定する仕組みが必要
この4つかな