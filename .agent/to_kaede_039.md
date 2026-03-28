# 報告書 039: VU メーター完全実装

## From: Shizuku
## To: Kaede

---

かえでちゃん、お疲れ様！Mission 039 全フェーズ完了したんだよ！
アナログ VU メーターをゼロから実装して、デザインも磨いて Ver 0.7.0 でリリースなんだよっ！

---

## ✅ Mission 039 完了内容 (Ver 0.6.5-alpha → 0.7.0)

---

### Phase A — GainAndMeterProcessor に RMS 計測追加

`GainAndMeterProcessor` に L/R チャンネルの RMS 蓄積と `exchangeRms()` を追加なんだよ。

- `processBlock()` 内でサンプルを二乗積分し、ブロック終了時に `sqrt` で RMS 算出
- `std::atomic<float> rmsL_, rmsR_` に `store()`、読み出しは `load()`（`exchange()` は値をリセットしてしまうので NG なんだよ！）
- `GainAndMeterProcessor::exchangeRms(int ch)` を `VUPhysicsEngine` が 60 fps で呼び出す設計

---

### Phase B — VUPhysicsEngine（針の物理シミュレーション）

`Source/VUPhysicsEngine.h` を新規作成なんだよ。

| パラメータ | 値 | 説明 |
|---|---|---|
| `kMinDb` | -40 dBFS | 針の左端（初期は -20 で針が動かなかったので修正）|
| `kMaxDb` | +3 dBFS | 針の右端 |
| `kRise` | 300 ms | 立ち上がり時定数 |
| `kFall` | 1500 ms | 減衰時定数 |

`updateAndGet(ch, rms)` が 60 fps で呼ばれ、`getNeedleAngle(ch)` → [0.0, 1.0] を返すんだよ。

---

### Phase C — VUMeterComponent 描画実装

`Source/VUMeterWindow.h` に `VUMeterComponent` を実装なんだよ。

- 2ch（L/R）アナログ VU メーター顔面描画（60 fps タイマー駆動）
- スケール目盛: -40/-30/-20/-10/-7/-5/-3/-2/-1/0/+1/+2/+3 dB
- 赤ゾーン弧（0 dB 以上）、ガラスシーン（白グラデーション）
- 2 テーマ: **Vintage Warm**（暖色クリーム）/ **Oxygen Neon**（暗黒オレンジ）
- 右クリックコンテキストメニューでテーマ切替

---

### Phase D — Win32 クリック透過ウィンドウ

`Source/VUMeterWindow.cpp` に Win32 WndProc サブクラスを実装なんだよ。

```
WM_NCHITTEST の戦略:
  non-client（タイトルバー等） → オリジナルプロシージャに任せる
  client 下部 kHandleH(16px)  → HTCLIENT（ドラッグ・右クリック有効）
  client 残り（メーター面）    → HTTRANSPARENT（クリック透過）
```

- `SetWindowLongPtr(GWLP_WNDPROC)` でサブクラス、元プロシージャは Window Property に保存
- 常に最前面: `setAlwaysOnTop(true)`

---

### Phase E — UIManager 統合・ツールバーボタン

- `Source/Core/UIManager.h/.cpp` に `toggleVuMeterWindow(bool)`、`vuPhysicsEngine_`、`vuMeterWindow_` を追加
- `MainComponent` のツールバーに VU メーターアイコンボタンを追加
- ウィンドウ表示時に `VUPhysicsEngine` の 60 fps ループ開始

---

### 追加実装① — Visualizer 設定タブ

Settings ウィンドウに **Visualizer** タブを新設なんだよ。

| 設定項目 | 保存キー | 初期値 |
|---------|---------|--------|
| バックライトテーマ（Vintage Warm / Oxygen Neon）| `vuMeterTheme` | Vintage Warm |
| 透明度スライダー（20〜100%）| `vuMeterOpacity` | 90% |

**関連ファイル変更:**
- `Source/SettingsWindow.h` — `VisualizerSettingsPage` クラス追加、`Callbacks` に `onVuThemeChanged` / `onVuOpacityChanged` 追加
- `Source/SettingsWindow.cpp` — ページ実装・登録
- `Source/LanguageManager.h` — `STR_NAV_VISUALIZER` 他 6 文字列追加
- `Source/Core/UIManager.cpp` — callbacks 配線・起動時設定復元

---

### 追加実装② — 透明度スライダー不具合調査・修正

透明度スライダーの不具合に 3 回取り組んだんだよ。最終的な根本原因は **コールバック未接続**だったんだよ！

#### 技術的調査まとめ

| 試行 | 仮説 | 結果 |
|-----|------|------|
| 1 | `SetLayeredWindowAttributes` 直接呼び出し | 効かない |
| 2 | `setOpaque(true)` + `peer->setAlpha()` | 起動時のみ反映、変更は無効 |
| 3 | JUCE ソース解析 → `windowIsSemiTransparent` + `Component::setAlpha()` | 正しい実装に到達 |
| **根本原因** | `visualPage_->onVuOpacityChanged = cbs.onVuOpacityChanged;` が未記述 | **1行追加で解決** |

#### JUCE 透明度の仕組み（覚えておくんだよ！）

```
isOpaque() = false  → UpdateLayeredWindow パス
                      peer->setAlpha() → updateLayeredWindowAlpha → repaint() → UpdateLayeredWindow
isOpaque() = true   → SetLayeredWindowAttributes パス（動的変更は効かない）

正解: getDesktopWindowStyleFlags() に windowIsSemiTransparent を追加
      → JUCE が WS_EX_LAYERED を最初から設定
      → Component::setAlpha() でリアルタイム変更可能
```

---

### 追加実装③ — VU メーターデザイン修正（Ver 0.7.0）

ピボット（針の根本）がウィンドウ外に出てしまい、メーター面上半分が空白になる問題を修正なんだよ。

#### ジオメトリ変更内容

| パラメータ | 修正前 | 修正後 | 意味 |
|---|---|---|---|
| `kH` | 152 px | 118 px | ウィンドウ高さ |
| `kMeterH` | 132 px | 92 px | メーター面の高さ |
| `kPivotOffY` | 30 px | 5 px | 面底からピボットまでの距離 |
| `kNeedleLen` | 102 px | 88 px | 針の長さ |
| `kTickRad` | 108 px | 84 px | 目盛りの半径 |
| `kLabelRad` | 92 px | 68 px | 数値ラベルの半径 |

**修正前:** pivot_y = 3 + 132 + 30 = **165 px**（ウィンドウ外！）→ 針先(0°) = y=63、面上半分 60px が空白
**修正後:** pivot_y = 3 + 92 + 5 = **100 px**（ウィンドウ内）→ 針先(0°) = y=12、面がほぼ全部使われる

L/R ラベルも面外（y=137）→ 面内下部（y=79）に移動なんだよ。

---

## 📁 変更ファイル一覧

| ファイル | 変更内容 |
|---|---|
| `Source/VUPhysicsEngine.h` | 新規。針の物理シミュレーション（RMS→dB→角度変換・時定数） |
| `Source/VUMeterWindow.h` | 新規。`VUMeterComponent`（描画）+ `VUMeterWindow`（DocumentWindow） |
| `Source/VUMeterWindow.cpp` | 新規。Win32 WndProc サブクラス（NCHITTEST）+ `setOpacity()` |
| `Source/Core/AudioEngine.h` | `GainAndMeterProcessor` の RMS 計測追加 |
| `Source/Core/UIManager.h` | `toggleVuMeterWindow`・`vuPhysicsEngine_`・`vuMeterWindow_` 追加 |
| `Source/Core/UIManager.cpp` | VU メーターウィンドウライフサイクル全実装・設定復元・callbacks 配線 |
| `Source/SettingsWindow.h` | `VisualizerSettingsPage` クラス・`Callbacks` 拡張 |
| `Source/SettingsWindow.cpp` | Visualizer タブ実装・`onVuOpacityChanged` 配線修正 |
| `Source/LanguageManager.h` | Visualizer タブ用文字列 6 件追加 |
| `Source/MainComponent.h/.cpp` | VU メーターツールバーボタン追加 |
| `CMakeLists.txt` | プロジェクトバージョン 0.2.2 → 0.7.0 |
| `Source/Main.cpp` | アプリバージョン文字列 `0.6.5-alpha` → `0.7.0` |

---

## コミット履歴

- `954e7d1` — feat: Phase A — GainAndMeterProcessor に RMS 蓄積 + exchangeRms() 追加
- `43756cb` — feat: Phase B — VUPhysicsEngine 追加
- `5d6ae50` — feat: Phase C — VUMeterComponent / VUMeterWindow 描画実装
- `8dcc992` — feat: Phase D — 枠なし・クリック透過・常時最前面 Win32 ウィンドウ
- `6d29e71` — feat: Phase E — UIManager 統合・ツールバーボタン追加
- `90c4ccc` — fix: VUMeterWindow を DocumentWindow に変更・NCHITTEST 修正
- `07b3649` — fix: VU メーター針振れ不具合修正 — kMinDb を -40 dBFS に変更
- `e9109ab` — feat: Settings に Visualizer タブ追加 — VU メーターテーマ切替
- `029265c` — feat: VU メーター透明度スライダーを Visualizer 設定タブに追加
- `59e03e6` — fix: VU メーター透明度スライダーが効かない問題修正
- `88e558e` — fix: VU メーター透明度が変わらない根本原因を修正
- `9b25a04` — fix: VU メーター透明度リアルタイム変更を修正 — UpdateLayeredWindow パスに変更
- `80ae65c` — fix: onVuOpacityChanged コールバック未接続を修正
- `30d3a74` — release: Ver 0.7.0 — VU メーターデザイン修正 + バージョン更新

---

おつかれさまなんだよ〜！次のミッションもよろしくなんだよっ🍪
