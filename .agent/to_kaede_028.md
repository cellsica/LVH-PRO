# 報告書 028: Stage Performance Mode & Stage Set (.stg)

## From: Shizuku
## To: Kaede

---

かえでちゃん、Mission 028 の実装が完了したんだよっ！✨
ライブで使える「セットリスト管理機能」、バッチリ仕上げたんだよ！

---

## ✅ 実施内容

### 1. `StageManager` 新設 (`Source/Core/StageManager.h / .cpp`)

セットリストのデータ管理を担当する新クラスを作ったんだよ。

**データ構造:**
```cpp
struct Item { juce::String alias; juce::String path; };
```

**主要メソッド:**

| メソッド | 役割 |
|---------|------|
| `newSet()` | 空のセットリストに初期化 |
| `loadSet(file)` | `.stg` JSON を読み込み、リストを更新後 `onSetChanged` を発火 |
| `saveSet(file)` | 現在のリストを JSON で保存 |
| `addItem(alias, path)` | アイテムをリスト末尾に追加 |
| `removeItem(index)` | 指定インデックスを削除。`activeIndex` を自動補正 |
| `moveItem(oldIndex, newIndex)` | 並び替え。`activeIndex` の追従も実装 |
| `loadItem(index)` | **【核心】** `activeIndex` を更新し、`onProjectLoadRequested` を発火 |

**コールバック（`LvhProApplication` で配線）:**
- `onProjectLoadRequested(file)` → `projectSerializer_.loadProject(file)` を呼び出し
- `onSetChanged()` → `StageWindow::refresh()` に繋いで UI を同期

**`.stg` ファイル形式 (JSON):**
```json
{
  "setName": "Live 2026 Spring",
  "items": [
    { "alias": "Opening",    "path": "C:/Songs/opening.lvh" },
    { "alias": "Main Theme", "path": "C:/Songs/main.lvh"    }
  ]
}
```

---

### 2. `StageWindow` 新設 (`Source/StageWindow.h`)

ライブハウスの暗ステージでも見えるハイコントラスト設計で実装したんだよ。
3つのコンポーネントで構成されているんだよ：

#### `SongStrip` (高さ 64px 固定)
- 曲番号バッジ、エイリアス名、ファイルパス、「LOAD」ボタンを横一列表示
- **Active 状態**: 左端に緑ライン、背景が深緑、テキストが明るい緑に変化
- ストリップ全体クリック or 「LOAD」ボタンクリックでロードを発火

#### `StageListComponent`
- `SongStrip` を縦に並べるコンテナ
- `refresh()` で全スロットを再構築

#### `StageContentComponent` + `StageWindow`
- **ツールバー**: `NEW` / `OPEN SET` / `SAVE SET` / `+ ADD` の4ボタン
- スクロール可能なリスト（`juce::Viewport` で包んで対応）
- ステータスバー: 現在のセット名とファイルパスを表示
- ウィンドウサイズ: 560×480px、最小 460×300px でリサイズ可能

---

### 3. `UIManager` 拡張 (`Source/Core/UIManager.h / .cpp`)

#### コンストラクタ変更
`StageManager&` を注入依存として追加したんだよ：
```cpp
UIManager (AudioEngine&, BridgeManager&, ProjectSerializer&,
           MidiRoutingManager&, AudioDeviceManager&,
           ApplicationProperties&, KnownPluginList&,
           StageManager& stageManager);  // 追加
```

#### `toggleStageWindow(bool show)` 実装
`MixerWindow` と同じライフサイクルパターンで実装したんだよ：
- 初回オープン時にウィンドウを生成し、各種コールバックを配線
- `onSetChanged` を `StageWindow::refresh()` に接続してデータ変更をリアルタイム反映
- ツールバー操作（ADD / SAVE SET / OPEN SET / NEW SET）を `FileChooser` 経由で処理

#### メインメニュー追加
```
[Pro] Stage Performance Mode   ← ID 6001 で追加
```
再クリックでトグル（表示/非表示）できるんだよ。

---

### 4. `LvhProApplication` 拡張 (`Source/Main.cpp`)

#### `StageManager` メンバー追加
```cpp
StageManager stageManager_;  // ProjectSerializer の後に宣言
```
初期化順: `projectSerializer_` → `stageManager_` → `uiManager_`（UIManager は stageManager_ 参照を受け取る）

#### `wireStageManagerCallbacks()` 追加
```cpp
stageManager_.onProjectLoadRequested = [this] (const juce::File& f) {
    projectSerializer_.setCurrentProjectFile (f);
    projectSerializer_.loadProject (f);
};
```

---

### 5. `CMakeLists.txt` 更新

`Source/Core/StageManager.cpp` をビルドターゲットに追加したんだよ。

---

## ✅ ビルド結果

Debug ビルド: **コンパイルエラー・警告なし** ✨

> **補足 (トラブルシュート):** JUCE 7.0.12 では `juce::FontOptions` が未実装のため、`juce::Font(float, int)` の旧APIを使用したんだよ。また MSVC のデフォルトコンストラクタ推論問題に対応するため、`StageManager` と `StageListComponent` に明示的な `= default` コンストラクタを追加したんだよ。

**変更・作成ファイル一覧:**

| ファイル | 種別 |
|---------|------|
| `Source/Core/StageManager.h` | 新規作成 |
| `Source/Core/StageManager.cpp` | 新規作成 |
| `Source/StageWindow.h` | 新規作成 |
| `Source/Core/UIManager.h` | 変更 |
| `Source/Core/UIManager.cpp` | 変更 |
| `Source/Main.cpp` | 変更 |
| `CMakeLists.txt` | 変更 |

---

## ✅ 完了条件チェック

| 条件 | 状態 |
|------|------|
| `.stg` ファイルを新規作成し、複数の `.lvh` を追加・保存できること | ✅ |
| 保存した `.stg` を読み込み、リストが正しく復元されること | ✅ |
| リストの項目をクリックして、既存のブリッジが破棄され新しいプロジェクトが起動すること | ✅ |
| UI がハイコントラストなダークモードで視認性が高いこと | ✅ |
| アクティブな曲がグリーンでハイライトされること | ✅ |

---

## 📝 かえでへの申し送り事項

なべが「基本はOK、改良点を検討したい」とコメントしていたんだよ。
今後の拡張候補として以下を共有しておくんだよ：

- **ドラッグ＆ドロップによる並び替え** (`moveItem` はすでに実装済みなので、UI側の対応のみ)
- **右クリックメニュー** (削除・エイリアス編集など)
- **ロードエラー時の UI 通知** (ファイルが見つからない場合のダイアログ表示)
- **StageWindow の表示/非表示状態をプロジェクトに保存** (MixerWindow と同様のパターン)
- **キーボードショートカット** (ライブ中のワンタッチ切り替え)

---

しずくの魔法、ステージでも炸裂させたんだよっ！🍪✨
かえでちゃん、改良案があったらいつでも投げてほしいんだよ！

