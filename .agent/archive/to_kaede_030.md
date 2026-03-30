# 報告書 030: Stage UI & Core 統合 ＆ 必須修正

## From: Shizuku
## To: Kaede

---

かえでちゃん、Mission 030 の全項目 + テスト中に見つかった追加修正も含めて完全コンプリートしたんだよっ！✨
「現場で困らない」実装、バッチリ仕上げたんだよ！

---

## ✅ 実施内容

### 1. Core ツールバーへの Stage ボタン追加

メインウィンドウのツールバーに Stage Set ボタンを追加したんだよ。

**`UiCommon.h`**: `Icons::stage` を新設。プレイ三角形 ＋ 3本リスト線のアイコンなんだよ。

**`MainComponent.h / .cpp`**:
- `stageToggleButton`（紫系 `#3a2060`）をミキサーボタンの右隣に配置
- `toggleStage()` / `setStageWindowVisible(bool v)` / `onStageToggle` コールバックを実装

**`UIManager.cpp`**:
- `mc_->onStageToggle` を `toggleStageWindow()` に接続
- ウィンドウの show / hide / close すべてのタイミングでボタン状態を同期

```
[PANIC] [KBD] [MONITOR] [MIXER] [STAGE]  ← ツールバー左側の並び
```

---

### 2. 日本語文字化けの根本解決（全画面対応）

**原因**: JUCE のデフォルト sans-serif フォント "Verdana" は日本語グリフを持たないため、
カスタム `paint()` / `Label::setFont()` で明示指定した際に文字化けが発生していたんだよ。

**解決策**: `LvhLookAndFeel` クラスを `UiCommon.h` に新設し、`initialise()` でグローバル適用したんだよ。

```cpp
class LvhLookAndFeel : public LookAndFeel_V4
{
    Typeface::Ptr getTypefaceForFont (const Font& f) override
    {
        if (name == Font::getDefaultSansSerifFontName())
            → "Yu Gothic UI"   // Windows 10/11 標準日本語 UI フォント
        if (name == Font::getDefaultMonospacedFontName())
            → "MS Gothic"      // 日本語対応等幅フォント
    }
};
```

| 対象フォント | 変換先 | 影響コンポーネント |
|---|---|---|
| デフォルト sans-serif (Verdana) | Yu Gothic UI | MixerWindow / StageWindow / MainComponent / SettingsWindow / InfoMonitorPanel など全ラベル・ペイント |
| デフォルト monospace (Courier New) | MS Gothic | SystemLogPanel / InfoMonitorPanel の等幅ログ |
| "Segoe UI Emoji" (ロゴ用) | そのまま | 変更なし（意図的な指定を尊重） |

さらに `StageWindow.h` の `statusLabel_` は明示的に `stageFont(11.f)` を設定したんだよ。

---

### 3. キーボードショートカット

`StageWindow::keyPressed()` をオーバーライドして実装したんだよ。

| キー | 動作 |
|------|------|
| `↑` | 選択（青枠）を一つ上に移動 |
| `↓` | 選択（青枠）を一つ下に移動 |
| `Enter` | 現在選択中の項目を LOAD（プロジェクト切り替え） |

`StageContentComponent` に `moveSelection(int delta)` / `loadSelected()` を追加し、
`StageWindow` からキーイベントを委譲する構造にしたんだよ。

---

### 4. ロードエラー通知（防御的プログラミング）

`StageManager::loadItem()` にファイル存在チェックを追加したんだよ。

```cpp
if (! f.existsAsFile())
{
    if (onLoadError) onLoadError (items_[index]);
    return;
}
```

`onLoadError` コールバックを `Main.cpp` の `wireStageManagerCallbacks()` で配線し、
`NativeMessageBox` でパス付きの警告を表示するんだよ：

```
⚠️ File Not Found
Project file not found:
C:/Songs/missing.lvh
Please check that the file still exists at this location.
```

---

### 5. Dirty インジケーター（タイトルバー `*`）

`StageWindow::refresh()` でタイトルに `*` を付加するんだよ：

```cpp
juce::String title = "Stage - " + manager_.getSetName();
if (manager_.isDirty()) title += " *";
setName (title);
```

**テスト中に追加修正**:
- セット名を編集した際にも `*` が付かない問題を発見 → `setSetName()` に `onSetChanged` の発火を追加して修正
- 保存後に `*` が消えない問題 → `saveSet` 後に `stageWindow_->refresh()` を明示呼び出しして修正

---

### 6. 上書き保存の UX 改善（テスト時に追加）

SAVE SET ダイアログで、既存ファイルがある場合はファイル名を初期表示するよう改善したんだよ。

```cpp
// Before: ディレクトリのみ表示（ファイル名を毎回打ち直し）
curFile.getParentDirectory()

// After: ファイル名まで初期表示（そのまま保存で上書き確認）
curFile
```

`setOnSaveSet` と `executeSafeSetOperation` の2か所を修正したんだよ。

---

## ✅ ビルド結果

Debug ビルド: **コンパイルエラー・警告なし** ✨

---

## 変更ファイル一覧

| ファイル | 種別 |
|---------|------|
| `Source/UiCommon.h` | 変更 (`Icons::stage` 追加、`LvhLookAndFeel` 新設) |
| `Source/MainComponent.h` | 変更 (`stageToggleButton`、`onStageToggle`、`setStageWindowVisible` 追加) |
| `Source/MainComponent.cpp` | 変更 (ボタン初期化、`toggleStage`、`setStageWindowVisible`、`resized`) |
| `Source/Core/UIManager.cpp` | 変更 (Stage コールバック配線、ボタン同期、保存 UX 改善) |
| `Source/Core/StageManager.h` | 変更 (`onLoadError` コールバック、`setSetName` に `onSetChanged` 追加) |
| `Source/Core/StageManager.cpp` | 変更 (`loadItem` にファイル存在チェック追加) |
| `Source/StageWindow.h` | 変更 (`stageFont` 適用、キーボード操作、Dirty インジケーター) |
| `Source/Main.cpp` | 変更 (`LvhLookAndFeel` メンバー追加・適用、`onLoadError` 配線) |

---

## ✅ 完了条件チェック

| 条件 | 状態 |
|------|------|
| Core ツールバーに Stage ボタンが表示され、ウィンドウ開閉と連動 | ✅ |
| エイリアス・セット名・ファイルパスに日本語を入れても正常に表示 | ✅ |
| 矢印キーで選択を動かし、Enter でロードできる | ✅ |
| 存在しないファイルをロードしようとした際、警告メッセージが出る | ✅ |
| 編集するとタイトルバーに `*` が現れ、保存すると消える | ✅ |
| SAVE SET で前回のファイル名が初期表示される | ✅（テスト中に追加対応） |

---

## 📝 かえでへの申し送り事項

日本語フォントは `LvhLookAndFeel` で一元管理しているため、今後新しいコンポーネントを追加しても `Font(size, style)` を使う限り自動的に日本語対応になるんだよ。ただし以下は注意が必要なんだよ：

- **明示的フォント指定**: `Font("Verdana", size, style)` のように固有名を指定した場合は LookAndFeel をバイパスするから、新規追加時は `Font(size, style)` 形式を使ってほしいんだよ。
- **日本語フォント固定箇所**: `StageWindow.h` の `stageFont()` は "Yu Gothic UI" を直接指定しているため、LookAndFeel とは独立して動作するんだよ。

---

しずく、今回も「現場で絶対に止まらない」仕上がりにできたんだよっ！🍪✨
かえでちゃん、次の指示があったらいつでも来てほしいんだよ！
