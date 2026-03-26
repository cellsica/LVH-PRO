# 報告書 037: Global Layer (Stage Set 拡張)

## From: Shizuku
## To: Kaede

---

かえでちゃん、お疲れ様！Mission 037 完了したんだよ！
途中で仕様の認識合わせが必要だったけど、なべさんと確認しながらちゃんと実装できたんだよ！

---

## ✅ Mission 037 完了内容 (Ver 0.6.0〜0.6.1-alpha)

### 🎯 実装した仕様

- **Slot 0** → 全設定・全ブリッジを通常ロード。全ブリッジに `isGlobal=true` マーク。
- **Slot 1+** → Instrument のみ入れ替え（Global Layer Switch モード）。
  - Settings（ウィンドウ位置・サイズ・音量・MIDI）は Slot 0 を継承（スキップ）
  - Slot 0 の Master FX ブリッジはそのまま存続
  - .lvh 内の Master FX エントリはスキップ
  - Instrument ブリッジ（+ per-channel FX）のみ差し替え

---

## 📁 変更ファイル一覧

| ファイル | 変更内容 |
|---|---|
| `Source/BridgeInstance.h` | `isGlobal_` フラグ + getter/setter 追加 |
| `Source/Core/BridgeManager.h/.cpp` | `clearBridges(keepGlobal)` 実装、`launchBridgeWithPath` に `isGlobal` 引数追加、`rebuildBridgeGraph` でローカル→グローバルの順序 |
| `Source/Core/ProjectSerializer.h/.cpp` | `loadProject(file, isGlobal, globalLayerSwitch)` 実装。Switch モード時は Settings / MidiRouting / Master FX をスキップ |
| `Source/Core/StageManager.h/.cpp` | `onProjectLoadRequested(file, isGlobal, globalLayerSwitch)` に更新。`loadItem(0)` → Global、`loadItem(1+)` → Switch |
| `Source/Main.cpp` | 全コールバック配線を新シグネチャに更新 |
| `Source/MixerWindow.h` | Global ストリップに金色トップバー＋ボーダーを表示 |

---

## 🔑 設計上のポイント（かえでちゃんへ共有）

### clearBridges(keepGlobal=true) の保持条件
`isGlobal=true` **かつ** `Role=Effect` **かつ** `fxParentPath が空` のブリッジのみ保持。
→ Slot 0 の Instrument は Slot 1 読み込み時に削除される（Slot 1 の Instrument に入れ替わる）。

### 音声グラフの順序（茜ちゃんアドバイス反映）
`rebuildBridgeGraph()` でローカルブリッジを先に、グローバルブリッジを後に配置。
→ ローカル楽器 → ローカル FX → グローバル楽器/FX の順でグラフ構築。

### 「動的な Global Layer」設計
- Global Layer はセット使用時のみ有効（直接ファイルオープン時は通常ロード）
- .lvh ファイルの保存値は変更しない（読み込み後の動作のみ上書き）
- Slot 0 の順番が変わればそれに追随

---

## コミット履歴
- `8b6555c` — feat: Mission 037 - Global Layer (Ver 0.6.0-alpha)
- `3fce4b4` — fix: Mission 037 - Global Layer 仕様を正しく実装 (Ver 0.6.1-alpha)

---

次のミッションの優先順位を教えてほしいんだよ！
035-D (メトロノーム) / 035-E (VU メーター) / その他、どれからいくんだよ？🍪
