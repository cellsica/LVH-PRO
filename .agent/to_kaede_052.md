# Mission 052 完了報告 — ThemePalette スキンファイル対応（完全版）

**担当:** しずく (Claude)  
**完了日:** 2026-04-03  
**ブランチ:** `feature/mission-052-theme-palette` / `feature/mission-052-light-theme-overhaul` → `develop` マージ済み

---

## 実装概要

外部 JSON ファイルから配色をロードする `ThemePalette` シングルトンを新設し、全 UI ソースのハードコード色を一掃しました。  
さらになべさんのリクエストで、Phase B として「動的テーマリスト」「カスタムテーマ」「Light テーマの本格配色」を追加実装しました。

---

## Phase A — ThemePalette コアシステム

### 変更ファイル一覧

| ファイル | 種別 | 内容 |
|---|---|---|
| `Source/Core/ThemePalette.h` | 新規 | `ColourId` 列挙体 (67色) + `ThemePalette` クラス宣言、`getDisplayName()` |
| `Source/Core/ThemePalette.cpp` | 新規 | シングルトン実装、JSON ローダー、Dark/Light デフォルト |
| `themes/dark.json` | 新規 | Dark テーマ定義 (67キー、実コードと照合して値を確定) |
| `themes/light.json` | 新規 | Light テーマ定義 (67キー) |
| `CMakeLists.txt` | 更新 | ThemePalette.cpp をソースに追加、themes/ コピー（copy_directory）、Deploy 追加 |
| `Source/Main.cpp` | 更新 | 起動時に `ThemePalette::load()` を実行 |
| `Source/SettingsWindow.h` | 更新 | `themeIds_` StringArray メンバー追加 |
| `Source/SettingsWindow.cpp` | 更新 | テーマ変更時に `themeName` 保存 + 再起動通知ダイアログ |

### Phase 3 リファクタリング（ハードコード色の一掃）

| ファイル | 置換箇所数 |
|---|---|
| `Source/MixerWindow.h` | 47箇所 |
| `Source/MainComponent.cpp` | 19箇所 |
| `Source/StageWindow.h` | 20箇所 |
| `Source/MetronomeWindow.h` | 11箇所 |
| `Source/SettingsWindow.cpp` | 18箇所 |
| `Source/LevelMeter.cpp` | 数箇所 |
| `Source/InfoMonitorPanel.cpp` | 数箇所 |
| `Source/SystemLogPanel.cpp` | 数箇所 |
| `Source/Core/PluginPickerComponent.cpp` | 数箇所 |
| `Source/UiComponents.cpp` | 数箇所 |
| `Source/Core/UIManager.cpp` | 数箇所 |
| `Source/UiCommon.h` | Icons 描画色 |

---

## Phase B — 動的テーマリスト

### 機能

- テーマ JSON 内に `"_name"` キーを設けることで表示名を定義（例: `"_name": "キャンパスノート"`）
- `ThemePalette::getDisplayName(File)` が `"_name"` を読み、なければファイル名ステムを使用
- Settings → General → Color Theme の ComboBox が `themes/` ディレクトリを動的スキャン
- `.json` を `themes/` に置くだけでアプリを再コンパイルせず選択肢に出現

### `_name` キーの規則

アンダースコアプレフィックス (`_name`, `_comment`) は JSON ローダーで色キーとして処理されず、メタデータとして扱われる。

---

## Phase C — カスタムテーマ「キャンパスノート」

**ファイル:** `themes/campus-note.json`

KOKUYO Campus ノートの質感をイメージした配色:

| 要素 | カラー | 印象 |
|---|---|---|
| 背景 | `#F7F3E8` | クリーム色の紙 |
| ボーダー | `#B8CCE0` | ノートの罫線（薄いブルー） |
| アクセントグリーン | `#4A9B6F` | KOKUYO 緑 |
| 選択状態 | `#A8C8E8` | 水色ハイライト |
| テキスト | `#1E1E2A` | 鉛筆書きの濃さ |

---

## Phase D — Light テーマ本格配色

**ファイル:** `themes/light.json`（完全書き直し）

### 設計方針

> 白背景では「明るいもの」ほど「見えなくなる」。Dark テーマと反対に、アクセント・メーター・ステート色はすべて **濃く・鮮やかに** する必要がある。

| 要素 | Before (旧 light) | After (新 light) | 理由 |
|---|---|---|---|
| `BgPanel` | `#F0F0F8` | `#FFFFFF` | パネルを純白に |
| `MeterYellow` | `#AAAA44` | `#BB8800` | 黄緑は白背景で不可視。濃い琥珀色に |
| `MeterGreen` | `#7ACC7A` | `#009933` | パステルでは視認性不足 |
| `CtrlActive` | `#A0B8D8` | `#2255AA` | くすんだ水色 → 強いブルー |
| `AccentGreen` | `#7ACC7A` | `#1A7A2A` | パステル → 深い緑 |
| `TextPrimary` | `#222233` | `#111120` | さらにニアブラックに強化 |

---

## ThemePalette アーキテクチャ（参考）

```
起動時
  Main.cpp::initialise()
    → prefs から "themeName" を取得（デフォルト: "dark"）
    → ThemePalette::getInstance().load("themes/<name>.json")
      - JSON 不在 / キー不足 → 組み込みデフォルトで補完
      - RRGGBB (6文字) / AARRGGBB (8文字) を受け入れ

UI 描画時
  g.setColour(ThemePalette::get(ColourId::BgPrimary));
```

### ColourId カテゴリ一覧 (67色)

| カテゴリ | 識別子数 |
|---|---|
| Backgrounds | 6 |
| Generic Controls | 3 |
| Mixer Special | 5 |
| Accent (toggle ON) | 7 |
| State | 8 |
| Text | 11 |
| Meters | 3 |
| Borders | 3 |
| Strip Palette | 6 |
| Stage | 6 |
| List | 2 |
| Settings | 4 |
| Metronome | 4 |

---

## 付帯修正 — ミキサーフェーダー目盛り（右側追加）

なべさんの追加リクエストにより、ミキサースプリッター内フェーダーの目盛りを **左右両端に対称表示** するよう修正。

- **変更箇所:** `Source/MixerWindow.h:446` `paintOverChildren()`
- **修正内容:** マイナー目盛り（0.05刻み）を左端のみから左右両端に拡張  
  （メジャー目盛り 0.0 / 0.5 / 1.0 / MAX はもとから全幅のため変更なし）
- **ブランチ:** `feature/mixer-fader-right-ticks` → `develop` マージ済み

---

## テーマ変更フロー（再起動ベース）

```
Settings → General → Color Theme → ComboBox 選択
    ↓ onChange
    prefs.setValue("colorTheme", 0 or 1)   ← Mission 051 LookAndFeel 互換
    prefs.setValue("themeName", "dark")     ← ThemePalette 用 (新規)
    AlertWindow → "再起動が必要です"
    ↓ 次回起動時
    Main.cpp → ThemePalette::load("themes/<name>.json")
```

---

## 制約事項

- **動的切り替え非対応:** 方針どおり再起動ベースの適用のみ
- **LookAndFeel との二重管理:** Mission 051 の `setTheme()` は JUCE 標準コントロール用として共存。ThemePalette はカスタム `paint()` を担当

---

> Mission 052 完全完了なんだよ！  
> `themes/` フォルダに `.json` を放り込むだけで新テーマが使える仕組みになったんだよっ！  
> Light もキャンパスノートも、白背景でメーターがクッキリ見えるように仕上げたんだよ！  
