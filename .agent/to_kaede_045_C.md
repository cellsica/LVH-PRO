# 報告書 045-C: ビジュアライザー拡張 — Phase C: Starfield Spectrum Visualizer

## From: Shizuku
## To: Kaede

---

かえでちゃん、お疲れ様なんだよ！  
Phase C の全タスク完了したんだよっ！宇宙ワープ空間、完成したんだよ！  
なべさんからのフィードバックを受けながら何度かブラッシュアップしたので、詳しく報告するんだよ！

---

## ✅ 実装内容

---

### 1. StarfieldVisualizer DLL (`Source/StarfieldVisualizer/StarfieldVisualizer.cpp`)

#### アーキテクチャ — 2 つの描画レイヤー

**レイヤー 1: アンビエントドット（常時表示）**

| 仕様 | 値 |
|---|---|
| サイズ | 常に 1px（遠近感なし） |
| 速度 | kDotSpeed = 0.0026f（非常にゆっくり） |
| スポーン方式 | 中央付近の小さな x,y でスポーン → z 減少で外側に広がる（ワープ投影） |
| 無音時スポーン間隔 | 14f ≒ 約4個/秒 |
| 演奏時スポーン間隔 | エネルギー連動で最短 4f ≒ 約15個/秒まで増加 |

演奏中は全バンドの平均エネルギーに応じてドット数が増加し、「場が騒がしくなる」演出を実現したんだよ。

**レイヤー 2: バンドタイル（音楽連動）**

16バンドを円形に配置し、各バンドの方向に向かってタイルが放射状に飛んでいくんだよ。

| 仕様 | 値 |
|---|---|
| バンド配置 | `angle[b] = b × 2π/16`（16方向均等配置） |
| タイルのワールド座標 | `(cos(angle) × 0.40, sin(angle) × 0.40)` |
| タイルの向き | 接線方向（円周に沿う方向）が幅広辺、半径方向が高さ辺 |
| 描画 | 回転クワッド（`juce::Path`）で正確に角度に合わせた矩形 |
| 速度 | エネルギーに応じて kTileSpeedMin(0.018) ～ kTileSpeedMax(0.095) |
| スポーン間隔 | 大音量=8f ～ 閾値付近=22f |

#### ドップラー赤方偏移カラーリング

```
energy=0（静か） → hue 0.65 = 青
energy=1（大音量）→ hue 0.0  = 赤
```

#### アタック→ディケイ演出（`life` フィールド）

タイルに `life` フィールドを追加したんだよ。

```
life = 1.0 at spawn
life -= speed / (kZFar - kZNear) per frame  →  z=kZNear に達すると 0
bri  = 0.25 + life × 0.75   （出音: 明るい → 減衰: 暗い）
alph = 0.15 + life × 0.85
```

鍵盤を弾いた瞬間に明るくスポーン → 飛びながら暗くなっていく自然なアタック/ディケイ感を実現したんだよ。

#### 軌跡（トレイル）

各タイルの後ろに 4 段階のフェードコピーを描画するんだよ。

```
step 4 (最古) → step 0 (現在) の順で描画（前のものが上に来る）
trailAlpha = step==0 ? 1.0 : (1 - step × 0.22)
trail_z = t.z + step × t.speed × 5.0
```

タイルの外縁（カメラ側）に白いハイライトストリップも追加してあるんだよ。

---

### 2. CMakeLists.txt への追加

`LVH-StarfieldVisualizer` ターゲットを追加。ビルド後に `<LVH.exe>/Visualizers/LVH-StarfieldVisualizer.dll` へ自動コピーされるんだよ。

---

### 3. SDK ドキュメント (`Source/VisualizerSDK/docs/`)

なべさんからのリクエストで、第三者がプラグインを開発するためのドキュメントを整備したんだよ。日英 2 言語で全 6 ファイル作成したんだよっ！

```
Source/VisualizerSDK/docs/
├── plugin-development-manual.ja.md   ← 開発環境・ビルド・導入マニュアル（日本語）
├── plugin-development-manual.en.md   ← 同（英語）
├── radial-visualizer-spec.ja.md      ← RadialVisualizer 仕様書（日本語）
├── radial-visualizer-spec.en.md      ← 同（英語）
├── starfield-visualizer-spec.ja.md   ← StarfieldVisualizer 仕様書（日本語）
└── starfield-visualizer-spec.en.md   ← 同（英語）
```

**開発マニュアルの主要コンテンツ**:
- SDK インターフェース仕様（`IAudioSource` / `IVisualizerPlugin` メソッド一覧）
- 必要ツールと JUCE の入手先
- `CMakeLists.txt` テンプレート（コピペ可）
- 最小構成プラグインの完全実装例
- ビルドコマンド・配置手順・動作確認方法
- FFT 値のスケール感・よくある問題と対処

**プラグイン仕様書の主要コンテンツ**:
- ビジュアルコンセプトと描画レイヤー構成
- アルゴリズム解説（数式付き）
- 全定数テーブル（変更効果の説明付き）
- バイブコーディング向けプロンプト例（AI にそのまま貼り付けられる形式）

---

## 📁 変更ファイル一覧

| ファイル | 変更内容 |
|---|---|
| `Source/StarfieldVisualizer/StarfieldVisualizer.cpp` | 新規：StarfieldVisualizer DLL 実装 |
| `CMakeLists.txt` | `LVH-StarfieldVisualizer` ターゲット追加 |
| `Source/VisualizerSDK/docs/*.ja.md` | 新規：日本語ドキュメント 3 ファイル |
| `Source/VisualizerSDK/docs/*.en.md` | 新規：英語ドキュメント 3 ファイル |

---

## コミット履歴（Phase C）

| ハッシュ | 内容 |
|---|---|
| `6604af5` | feat: Mission 045-C - Starfield Spectrum Visualizer (初期実装) |
| `288d336` | feat: StarfieldVisualizer redesign - ambient dots + 16-band LED tiles |
| `15c67e7` | feat: 円形16バンド配置 + 接線方向回転クワッド |
| `29af4c2` | tweak: タイルスポーンレート削減 |
| `ac909c1` | feat: アタック/ディケイ演出 (life フィールド) |
| `b0a3dc5` | feat: 演奏中のアンビエントドット増加 (エネルギー連動) |
| `817e5dd` | docs: SDK ドキュメント整備 (JA/EN 全6ファイル) |

---

## ✅ 動作確認

- LVH.exe ビルド成功 ✅
- LVH-StarfieldVisualizer.dll ビルド成功 ✅（`<exe>/Visualizers/` へ自動コピー）
- 無音時: 1px ドットがゆっくりと中央から流れ出す ✅
- 演奏時: 16方向に青〜赤のタイルが放射状に飛び出す ✅
- アタック/ディケイ: 出音は明るく、飛ぶにつれて暗くなる ✅
- 演奏中にドット数が増加して場が賑やかになる ✅
- RadialVisualizer と共存可能（Visualizers/ に両 DLL 配置） ✅

---

## 📝 今後の拡張メモ

- **プラグイン切り替え UI**: 現状は全 DLL を同時描画。右クリックメニュー等での選択機能を将来追加可能
- **OpenGL ハードウェアレンダリング**: 現状は JUCE ソフトレンダリング。パーティクル数をさらに増やす場合は GPU 描画への移行も選択肢
- **iOS / macOS 対応**: SDK ヘッダーはクロスプラットフォーム対応済み。DLL エントリーポイントのみプラットフォーム別調整が必要

Mission 045 全フェーズ（A・B・C）、完走したんだよっ！お疲れ様なんだよ！🍪🌟
