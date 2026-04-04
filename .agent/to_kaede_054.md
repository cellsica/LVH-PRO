# Mission 054 完了報告 — Instrument Layout Studio Phase A (データモデル & MIDIルーティングバックエンド)

**担当:** しずく (Claude)  
**完了日:** 2026-04-04  
**ブランチ:** `feature/mission-054-layout-studio-phase-a` → `develop` マージ済み

---

## 実装概要

Instrument Layout Studio の心臓部となるデータモデルと、ブロックベースの MIDI ルーティングエンジンを構築しました。UI は次フェーズ以降で実装します。

---

## 実装内容

### 新規ファイル: `Source/Core/KeyboardBlock.h`

鍵盤分割の単位となるブロック構造体を定義しました。

```cpp
struct KeyboardBlock
{
    int          startNote       = 48;         // 範囲開始 MIDI ノート番号
    int          endNote         = 59;         // 範囲終了 MIDI ノート番号
    int          octaveShift     = 0;          // ブロック別オクターブシフト (±3)
    juce::String targetPluginPath;             // 送信先ブリッジの識別キー
    juce::Colour blockColour     { 0xff556688u }; // 表示色 (Phase C で Mixer CH と同期)

    // ランタイム専用 — シリアライズ対象外
    BridgeInstance* targetBridge = nullptr;    // resolveBlockTargets() で解決

    bool containsNote (int note) const noexcept;
    int  shiftedNote  (int rawNote) const noexcept;
};
```

**設計判断:**
- ブリッジ識別は UUID でなく `pluginPath`（既存コードベース全体で一貫）
- `targetBridge` はランタイムキャッシュとして保持し、保存/読み込みの対象外

---

### 変更: `Source/Core/MidiRoutingManager.h / .cpp`

#### sendMidi() の拡張

ブロック定義の有無で2系統のルーティングを切り替えます。

```
ブロックあり:
  NoteOn  → 音域が一致するすべてのブロックへ (octaveShift 適用)
  NoteOff → NoteOn 時に記録した (bridge, shiftedNote) へ正確に配信
  非ノート → 全ブリッジにブロードキャスト (CC, PitchBend, Sustain 等)

ブロックなし (フォールバック):
  従来の routeToAll / 単一ターゲット ルーティングをそのまま使用
```

#### レイヤー対応 — activeNoteTargets_ ノートトラッカー

同一音域に複数ブロックが重なるレイヤー構成で、NoteOff が正しいブリッジへ届くよう、NoteOn 時の送信先を記録します。

```cpp
struct ActiveNoteEntry { BridgeInstance* bridge; int shiftedNote; };
std::map<int, std::vector<ActiveNoteEntry>> activeNoteTargets_;
// キー: 生の受信ノート番号 → 値: NoteOn を送った (bridge, シフト後ノート) のリスト
```

これにより「NoteOn 時と異なるブロック設定でノートが離鍵された場合」でも、どのブリッジに NoteOff を送るべきかが一意に決まります。

#### ブリッジライフサイクルとの同期

| メソッド | 呼び出しタイミング |
|---|---|
| `notifyBridgeConnected(b)` | ブリッジ接続時 (Main.cpp の `onApplyPendingMidiTarget` 内) |
| `handleBridgeDisconnected(b)` | ブリッジ切断時 (既存コール箇所を拡張) |
| `resolveBlockTargets()` | `setBlocks()` 後に自動実行 |

#### スレッド安全性

`keyboardBlocks_` と `activeNoteTargets_` を `juce::CriticalSection` (`blockRoutingLock_`) で保護しています。MIDI スレッドと メッセージスレッドの両方から安全にアクセスできます。

#### 追加 API

```cpp
void setBlocks (std::vector<KeyboardBlock> blocks); // ブロックリスト一括更新
std::vector<KeyboardBlock> getBlocks() const;        // スレッドセーフなコピー取得
void clearBlocks();                                  // 全ブロック削除 → レガシー復帰
bool hasBlocks() const noexcept;
void resolveBlockTargets() noexcept;
void notifyBridgeConnected (BridgeInstance* b) noexcept;
```

#### resetForProjectLoad() の拡張

プロジェクトロード時のフルリセットで `clearBlocks()` も実行するよう変更しました。

---

### 変更: `Source/Core/ProjectSerializer.cpp`

`.lvh` ファイルに `<LayoutStudio>` セクションを追加しました。

**保存フォーマット (XML):**
```xml
<LayoutStudio>
  <Block startNote="48" endNote="59" octaveShift="-1"
         targetPluginPath="/path/to/plugin.vst3"
         colour="ff2255aa"/>
  <Block startNote="60" endNote="83" octaveShift="0"
         targetPluginPath="/path/to/other.vst3"
         colour="ff44aa55"/>
</LayoutStudio>
```

**読み込み時の動作:**
- `<LayoutStudio>` セクションあり → ブロックを復元、`resolveBlockTargets()` で接続中ブリッジに自動リンク
- `<LayoutStudio>` セクションなし → `clearBlocks()`（従来ルーティングへフォールバック）
- `globalLayerSwitch` モード (Stage Set 曲切り替え) → スキップ（Slot 0 の設定を継承）

---

### 変更: `Source/Main.cpp`

```cpp
bridgeManager_.onApplyPendingMidiTarget = [this] (const juce::String& path, BridgeInstance* b) {
    midiRouter.tryApplyPendingTarget (path, b);
    midiRouter.notifyBridgeConnected (b);  // ← 追加: ブロックターゲットを自動解決
};
```

---

## 事前確認事項への回答 (指示書 §3)

**下位互換性について:**  
ブロックが1つも定義されていない場合は従来の「MIDIチャンネルによる一括ルーティング」に完全フォールバックします。既存プロジェクトファイルに `<LayoutStudio>` セクションがなければ、`clearBlocks()` が呼ばれ従来動作を維持します。

**重複ノート・レイヤー時の NoteOff 不整合について:**  
`activeNoteTargets_` ノートトラッカーにより解決しています。NoteOn 時に実際に送信した (bridge, shiftedNote) のペアを記録し、NoteOff 時は同じペアに送信します。ノート保持中にブロック設定が変わっても、スタックノートが残りません。

---

## ルーティングの全体フロー (Phase A 完成後)

```
MIDI 入力 (ハードウェアコントローラー)
    ↓
MidiRoutingManager::sendMidi()
    ├─ ブロックあり?
    │   ├─ NoteOn: 音域一致ブロック全件へ (octaveShift 適用) + トラッキング記録
    │   ├─ NoteOff: トラッキングテーブルから (bridge, shiftedNote) を検索して送信
    │   └─ 非ノート: 全ブリッジへブロードキャスト
    └─ ブロックなし: 従来 routeToAll / 単一ターゲット
```

---

> Mission 054 Phase A 完了なんだよ！  
> UI がなくても MIDI が正しく振り分けられるバックエンドができたんだよっ！  
> Phase B（仮想キーボード UI）に向けてデータモデルはしっかり固まったんだよ！
