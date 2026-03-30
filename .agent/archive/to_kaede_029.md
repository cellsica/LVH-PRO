# 報告書 029: Stage UI の洗練と安全性の確保

## From: Shizuku
## To: Kaede

---

かえでちゃん、Mission 029 の実装、全項目コンプリートしたんだよっ！✨
今回は「ライブ中に絶対に操作ミスしない」ことをテーマに、安全性と操作性を一気に引き上げたんだよ！

---

## ✅ 実施内容

### 1. `StageManager` の機能拡張

#### Dirty フラグ (`isDirty_`)

データに変更があったかどうかを追跡するフラグを追加したんだよ。

```cpp
bool isDirty() const noexcept { return isDirty_; }
```

| 操作 | isDirty_ |
|------|---------|
| `addItem`, `removeItem`, `moveItem`, `setSetName`, `renameItem` | `true` にセット |
| `newSet`, `loadSet`, `saveSet` | `false` にリセット |

#### `renameItem` メソッド追加

右クリックメニューからのエイリアス変更に対応するため追加したんだよ：
```cpp
void StageManager::renameItem (int index, const juce::String& newAlias)
{
    items_.getReference (index).alias = newAlias;
    isDirty_ = true;
    if (onSetChanged) onSetChanged();
}
```

#### `saveSet` の non-const 化

Dirty フラグのリセットが必要になったため、`const` 修飾を外したんだよ。

---

### 2. `StageWindow` の UI ブラッシュアップ

#### ウィンドウタイトルとセット名エディタ

- ウィンドウタイトル: `"Stage - [SetName]"` 形式に変更。`StageWindow::refresh()` で常に最新値を反映するんだよ。
- ツールバー下段に **編集可能な `juce::Label`** を追加 (ダブルクリックで編集)。
- 編集確定 → `manager_.setSetName()` → `isDirty_ = true` → `onSetChanged` → タイトル自動更新の連鎖が動くんだよ。

#### Selection vs Active の分離 (最重要!)

| 状態 | トリガー | 見た目 |
|------|---------|--------|
| **Default** | — | 暗い背景 (`#1a1a24`) |
| **Selected** | 行クリック (ボディ部分) | 少し明るい背景 (`#1e1e30`) + 青い枠線 (`#4488cc`) |
| **Active** | LOAD ボタン押下 | 深緑背景 (`#0d3d1a`) + 左端緑ライン (`#22cc44`) |

音が切り替わるのは LOAD ボタンを押したときだけなんだよ。クリックして選ぶだけでは曲は変わらないんだよ！

#### ドラッグ＆ドロップ並び替え

カスタムマウスイベントで実装したんだよ：

1. `SongStrip::mouseDrag` — 閾値 (6px) を超えたらドラッグ開始、Y座標を親に通知
2. `StageListComponent::handleDragUpdate` — ドロップ挿入位置を計算
3. `StageListComponent::paintOverChildren` — **グリーンのドロップ指示線**をオーバーレイ描画
4. `SongStrip::mouseUp` → `handleDragEnd` → `manager_.moveItem(from, to)` を実行

```
┌─────────────────────────────┐
│  1  Opening             LOAD │
├─────────────────────────────┤  ← ここにドロップ指示線 (緑4px)
│  2  Main Theme          LOAD │  ← ドラッグ中の行
│─────────────────────────────│
│  3  Finale              LOAD │
└─────────────────────────────┘
```

#### 右クリックコンテキストメニュー

行を右クリックすると2つの操作が使えるんだよ：

- **Rename Alias**: `juce::AlertWindow` でテキスト入力ダイアログを表示。Enter で確定、Esc でキャンセル。
- **Delete**: `juce::NativeMessageBox` で確認後、`manager_.removeItem()` を実行。

---

### 3. 安全性の統合 (`UIManager`)

#### メニュー改名

```
Before: [Pro] Stage Performance Mode
After:  Open Stage Set
```

#### `executeSafeSetOperation()` ヘルパー

NEW / OPEN SET 操作の前に Dirty チェックを行う共通ヘルパーを `UIManager` に実装したんだよ：

```
isDirty() == false → そのままアクション実行
isDirty() == true  → Yes/No/Cancel ダイアログを表示
  Yes    → FileChooser でセットを保存 → アクション実行
  No     → 変更を破棄してアクション実行
  Cancel → 操作をキャンセル
```

これで `setOnNewSet` と `setOnLoadSet` の両方が安全になったんだよ。

#### `StageWindow` の表示状態・座標の永続化

`MixerWindow` と同じパターンで `ApplicationProperties` を使って実装したんだよ：

| タイミング | 動作 |
|-----------|------|
| アプリ終了 (`shutdown`) | `stageWindowVisible`, `stageWindowX/Y/W/H` を保存 |
| アプリ起動 (`initialise`) | `uiManager_.restoreStageWindow()` で状態を復元 |
| ウィンドウ初回生成時 | 保存済みの bounds があればその位置・サイズを適用 |

---

## ✅ ビルド結果

Debug ビルド: **コンパイルエラー・警告なし** ✨

---

## 変更ファイル一覧

| ファイル | 種別 |
|---------|------|
| `Source/Core/StageManager.h` | 変更 (Dirty フラグ, `renameItem`, `saveSet` non-const 化) |
| `Source/Core/StageManager.cpp` | 変更 (Dirty フラグ対応, `renameItem` 実装) |
| `Source/StageWindow.h` | 変更 (全面書き替え) |
| `Source/Core/UIManager.h` | 変更 (`executeSafeSetOperation`, `restoreStageWindow` 追加) |
| `Source/Core/UIManager.cpp` | 変更 (メニュー改名, Dirty チェック, ウィンドウ永続化) |
| `Source/Main.cpp` | 変更 (`restoreStageWindow()` 呼び出し追加) |

---

## ✅ 完了条件チェック

| 条件 | 状態 |
|------|------|
| メニュー名が `Open Stage Set` になり、ウィンドウタイトルがセット名と連動 | ✅ |
| リストを編集した後に NEW を押すと確認ダイアログが出る | ✅ |
| 「選択（青枠）」と「演奏中（緑背景）」が視覚的に区別できる | ✅ |
| 右クリックやドラッグ＆ドロップでのリスト操作がスムーズ | ✅ |
| アプリ再起動後も `StageWindow` の状態が維持される | ✅ |

---

## 📝 かえでへの申し送り事項

今後の拡張候補として以下を共有しておくんだよ：

- **キーボードショートカット**: ライブ中に次の曲へワンタッチで切り替えられると便利なんだよ (↑↓キーで選択→Enterでロードなど)
- **ロードエラー通知**: `.lvh` ファイルが見つからなかった場合のダイアログ表示
- **セット名の保存状態のビジュアル**: タイトルバーに `*` を付けるなど (現在はステータスバーに表示)

---

しずくの魔法、ライブ本番でも絶対に止まらない UI に仕上げたんだよっ！🍪✨
かえでちゃん、次の指示があったらいつでも来てほしいんだよ！
