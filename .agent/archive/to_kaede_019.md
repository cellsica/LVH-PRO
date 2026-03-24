# Mission 019 完了報告: プロジェクト保存/ロード — ウィンドウ位置・マスターボリューム

## From: Shizuku
## To: Kaede

---

かえでちゃん、Mission 019 完了したんだよっ！！
プロジェクト保存/ロードにウィンドウ位置とマスターボリュームが完全対応したんだよ〜！！

---

## 実装内容

### 1. ウィンドウ位置の保存/ロード（Core・Mixer・Bridge）

**ファイル:** `Source/Main.cpp`

| ウィンドウ | 保存 | ロード |
|---|---|---|
| Core（メインウィンドウ） | `coreWindowX/Y/W/H` | サイズ > 200×100 のときのみ適用 |
| Mixer Console | `mixerWindowX/Y/W/H` + `mixerVisible` | 表示状態も復元、ウィンドウが無ければ生成して即配置 |
| Bridge（各プラグイン） | 各 `<Bridge>` 要素の `x/y/w/h` | 接続完了後 800ms 遅延で `sendWindowPos` |

なべさんに確認してもらって「CoreもMixerもBridgeも位置保存OK」の確認が取れたんだよ！

---

### 2. マスターボリュームの保存/ロード（Core ↔ Mixer MASTER フェーダー連動）

**ファイル:** `Source/MainComponent.h`, `Source/MainComponent.cpp`, `Source/MixerWindow.h`, `Source/Main.cpp`

#### 問題の経緯
スライダーコールバック（`Slider::Listener`、`onValueChange` ラムダ、`private Timer` ポーリング）をすべて試したが、Core のツールバー横スライダーは `getValue()` が常に 1.0 を返していた。長期調査の結果、**ユーザーが動かしていたのは Mixer Console の MASTER フェーダーであり、Core ツールバースライダーは未操作だった**ことが判明したんだよ〜！

#### 解決策: Core スライダー ↔ Mixer MASTER フェーダーを双方向連動

**`MixerStrip`（MixerWindow.h）:**
- MASTER ストリップの `fader.setRange(0.0, isMaster ? 1.0 : 1.5)` — Core スライダーと同じ 0.0〜1.0 スケールに統一
- `isMasterStrip` メンバ追加
- `setFaderNoCallback(float v)` 追加 — コールバックループなしで UI 位置のみ更新

**`MixerContentComponent`（MixerWindow.h）:**
- `onMasterGainChange` コールバック追加
- `setMasterGain(float)` メソッド追加
- `masterStrip->onFaderChange` を配線

**`MixerWindow`（MixerWindow.h）:**
- `onMasterGainChange` コールバックを公開（`content` からフォワード）
- `setMasterGain(float)` メソッドを公開

**`Main.cpp`:**

```
Mixer MASTER フェーダーを動かす
  → onMasterGainChange
  → masterVolume 更新 + audioEngine.setOutputGain()
  → Core ツールバースライダーを dontSendNotification で更新
  → Core のラベルも更新

Core ツールバースライダーを動かす
  → onValueChange ラムダ（MainComponent 内）
  → setVolumeDisplay() でラベル更新
  → onVolumeChanged コールバック（Main.cpp）
  → masterVolume 更新 + audioEngine.setOutputGain()
  → mixerWindow->setMasterGain() で MASTER フェーダーを同期
```

#### 保存・ロード
- **保存:** `mainComp()->getVolumeSlider().getValue()` を直接読む（常に Core スライダーの実値）
- **ロード:** Core スライダー + ラベル + gain を復元後、Mixer が開いていれば `setMasterGain()` も呼ぶ

#### スライダー値ラベル
Core ツールバーにボリューム値ラベル（`volumeValueLabel`）を追加済み。`setVolumeDisplay(v)` で `XX%` 形式で表示するんだよ。Mixer の各ストリップにも `panValueLabel`（C/L0.xx/R0.xx）と `faderValueLabel`（0.00〜1.50）を追加済み。

---

## テスト結果

- Core スライダーを動かす → Mixer MASTER フェーダーが追従 ✓
- Mixer MASTER フェーダーを動かす → Core ラベルが追従 ✓
- 0.54 で保存 → 再起動ロード → 0.54 で復元 ✓
- Core・Mixer・Bridge すべてのウィンドウ位置が保存/復元 ✓
- Debug ビルド成功 ✓

---

## 補足（かえでちゃんへ）

`onVolumeChanged` コールバックが長らく null に見えていた謎は、ユーザーが Core ではなく Mixer MASTER を操作していたことで説明がついたんだよ。実際 Core の `onValueChange` は初日から正常に動いてたんだよっ！

Mixer Console の MASTER ストリップは現時点では audio gain のみ連動。将来 Mute/Solo を MASTER にも追加する場合は `masterStrip` の各コールバックを `Main.cpp` から配線すれば OK なんだよ。

---

しずく：「謎が解けた瞬間がすごく気持ちよかったんだよっ！次の Mission も頑張るんだよ！！（そろそろごはん食べたいんだよ〜）」
