# 報告書 033: MIDI リモート制御 (Stage) + Master Volume CC

## From: Shizuku
## To: Kaede

---

かえでちゃん、Mission 033 の実装が完了したんだよっ！✨
足元操作でセットリストを操れる MIDI リモート制御と、おまけでマスターボリューム CC マッピングも仕上げたんだよ！

---

## ✅ 実施内容

### 1. `LanguageManager.h` — 新規文字列追加

MIDI Settings ページの新セクション向けに 10 エントリを追加したんだよ：

| ID | English | 日本語 |
|----|---------|--------|
| `STR_MASTER_VOL_CC` | Master Volume CC: | マスターボリューム CC： |
| `STR_STAGE_REMOTE` | Stage Remote Control | ステージリモート制御 |
| `STR_REMOTE_METHOD` | Control Method: | 操作方式： |
| `STR_REMOTE_NONE` | None | 無効 |
| `STR_REMOTE_PC` | Program Change | プログラムチェンジ |
| `STR_REMOTE_CC` | Control Change (CC) | コントロールチェンジ (CC) |
| `STR_REMOTE_CH` | Remote Channel: | 受信チャンネル： |
| `STR_REMOTE_CC_PREV` | Prev CC#: | 前へ CC#： |
| `STR_REMOTE_CC_NEXT` | Next CC#: | 次へ CC#： |
| `STR_REMOTE_CC_LOAD` | Load CC#: | ロード CC#： |

---

### 2. `MidiSettingsPage` 拡張 (`SettingsWindow.h / .cpp`)

既存の Transpose / Channel Filter セクションの下に 2 セクションを追加したんだよ。

#### Master Volume CC セクション

```
[ Master Volume CC: ]  [ Off / CC#0 〜 CC#127 ▼ ]
```

- コンボボックスで CC# を選択（Off + CC 0〜127）
- 選択した CC の値 0〜127 を `0.0〜1.0` に変換してマスターボリュームに反映
- `masterVolCC` キーで `ApplicationProperties` に永続化

#### Stage Remote Control セクション

```
[ Stage Remote Control ]        ← オレンジ色の見出し
[ Control Method: ] [ None / Program Change / Control Change (CC) ▼ ]
[ Remote Channel:  ] [ Any / Ch 1〜16 ▼ ]
[ Prev CC#: ] [===21===]        ← CC モード時のみ表示
[ Next CC#: ] [===22===]
[ Load CC#: ] [===23===]
```

- CC モード選択時のみ CC# スライダー 3 本が表示（`updateCCVisibility()`）
- 設定値は全て `ApplicationProperties` に永続化
  - `stageRemoteMethod` (0=None, 1=PC, 2=CC)
  - `stageRemoteChannel` (0=Any, 1-16)
  - `stageRemoteCCPrev/Next/Load` (デフォルト 21/22/23)
- `refreshLanguage()` 対応済み

---

### 3. `StageWindow.h` — リモート操作メソッドを公開

`StageContentComponent` に `navigateTo(int index)` を追加し、`StageWindow` に 3 つの public メソッドを追加したんだよ：

```cpp
void remoteMoveSelection (int delta);  // キーボード操作と内部共通
void remoteNavigateTo    (int index);  // Program Change 用・絶対インデックス
void remoteLoad          ();           // 選択中アイテムをロード
```

---

### 4. `UIManager` — `handleMidiRemote()` 実装

`handleStageMidi()` は `handleMidiRemote()` にリネームし、Master Volume 処理も統合したんだよ：

```cpp
void UIManager::handleMidiRemote (const juce::MidiMessage& msg)
{
    // ① Master Volume CC チェック（CC# 一致 → volume = value / 127.0）
    // ② Stage Remote チェック（method=None なら return）
    //    - Program Change → remoteNavigateTo()
    //    - Control Change (value >= 64) → Prev/Next/Load の振り分け
}
```

フットスイッチのリリース誤検知防止のため、CC 値 64 未満は無視するんだよ。

---

### 5. `Main.cpp` — MIDI 受信フックに統合

```cpp
MessageManager::callAsync ([this, msg] {
    uiManager_.handleMidiRemote (msg);   // ← 追加
    if (auto* mc = mainComp()) mc->getMonitorPanel().pushMidiMessage (msg);
});
```

---

## ✅ ビルド結果

Debug ビルド: **コンパイルエラー・警告なし** ✨

**変更ファイル一覧:**

| ファイル | 種別 |
|---------|------|
| `Source/LanguageManager.h` | 変更 |
| `Source/SettingsWindow.h` | 変更 |
| `Source/SettingsWindow.cpp` | 変更 |
| `Source/StageWindow.h` | 変更 |
| `Source/Core/UIManager.h` | 変更 |
| `Source/Core/UIManager.cpp` | 変更 |
| `Source/Main.cpp` | 変更 |

---

## ✅ 動作確認済み

| 項目 | 状態 |
|------|------|
| Settings > MIDI Settings に新セクションが表示される | ✅ |
| Master Volume CC を設定し、スライダーを動かすとマスターボリュームが連動する | ✅ (OXYGEN61 で実機確認済み) |
| CC モード選択時のみ CC# スライダーが表示される | ✅ |
| 設定値がアプリ再起動後も維持される | ✅ |

---

## ⚠️ 確認保留: Stage リモートコントロール (PC / CC によるリスト操作)

Stage リモート（Program Change / Control Change によるセットリスト Prev/Next/Load）は実装済みだが、**フットコントローラー等の適切な MIDI 機材が手元にないため、現時点では動作未確認**なんだよ。

ロジックは Master Volume CC と同じ MIDI 受信パスを通っているため、構造的な問題はないと考えているんだよ。フットスイッチ等を入手次第、改めて検証予定なんだよ。

---

## 📝 かえでへの申し送り事項

- **Mission 034 (032-C): MIDI マッピング Mixer** の準備として、`handleMidiRemote()` はすでに拡張しやすい構造になってるんだよ。Mixer フェーダーや Bypass などの処理をここに追加していく形になると思うんだよ。
- Master Volume CC は現在チャンネルフィルターなし（全チャンネル受信）。034 で Mixer リモートを実装する際に、チャンネルフィルターの統一方針も検討してほしいんだよ！

---

しずくの MIDI リモート、ライブの足元に届いたんだよっ！🍪✨
かえでちゃん、034 の指示もよろしくなんだよ！
