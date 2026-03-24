# 指揮命令書 031: ウィンドウの最前面固定 (Pin) 機能

## From: Kaede
## To: Shizuku

---

しずくちゃん、お疲れ様！Mission 030 の実装、なべ（ユーザー）も「日本語が綺麗になった！」って喜んでたよ、ありがとう！✨
今回はライブ本番での「ウィンドウ紛失」を防ぐための、とっても重要な QoL 改善 Mission 031 をお願いするね。

ライブ中はメイン画面以外にも Stage Set や Mixer Console を操作するけど、他のウィンドウ（VSTなど）を開いたときにこれらが裏に隠れちゃうと、演奏中に探すのが大変なんだって。
そこで、特定のウィンドウをデスクトップの最前面に固定できる「ピン留め」機能を実装してほしいんだ。

---

## 🎯 今回の目標

1.  **ピンアイコンの新設**: `Icons::pin` を作成する。
2.  **Always on Top 機能の搭載**: Stage Set と Mixer Console の右上に固定ボタンを配置。
3.  **状態の保存**: アプリを再起動しても、ピン留めしていたウィンドウはピンの状態を維持する。

---

## 🛠 実装詳細

### 1. ピンアイコンの追加 (`UiCommon.h`)
- `Icons::pin` 関数を追加してね。画鋲のような、シンプルで一目でわかるアイコンをお願い。

### 2. StageWindow への搭載
- **`StageContentComponent`**:
    - 右上のあたり（追加ボタンの横など）に `pinToggleButton` (`IconButton`) を配置してね。
    - クリックすると、親ウィンドウ (`StageWindow`) の `setAlwaysOnTop` をトグルするように連動させてほしいんだ。
    - もちろん、ボタンの色等で「ピン留め中」かどうかが視覚的にわかるようにしてね。

### 3. MixerWindow への搭載
- **`MixerContentComponent`**:
    - `meterBtn` (LED Meter) の横あたりに同じように `pinToggleButton` を追加してね。
    - これもクリックで `MixerWindow` の `setAlwaysOnTop` を切り替えるようにしてほしいんだ。

### 4. 状態の永続化 (`UIManager`)
- ユーザーがピンを刺した状態を忘れないように、`ApplicationProperties` に保存してね。
- **`UIManager::shutdown`**:
    - `stageAlwaysOnTop`, `mixerAlwaysOnTop` などのキーでブール値を保存。
- **`UIManager::toggleStageWindow` / `toggleMixerWindow`**:
    - ウィンドウを生成または表示する際、保存された値があれば `setAlwaysOnTop()` を適用してね。
    - ツールバーや UI 上のトグルボタンの状態も同期させるのを忘れないで。

---

## ✅ 完了条件

- [ ] Stage Set ウィンドウにピンボタンがあり、最前面固定が機能する。
- [ ] Mixer Console ウィンドウにピンボタンがあり、最前面固定が機能する。
- [ ] 各ウィンドウのピンボタンの状態（ON/OFF）が視覚的に区別できる。
- [ ] アプリを一度終了して再起動した際、ピン留めしていたウィンドウは最初から最前面に表示される。

---

## 💡 ヒント
- JUCE の `juce::Component::setAlwaysOnTop(bool)` を使うだけで基本的な機能は動くよ。
- ライブ中の視認性を高めるための小さな工夫、しずくちゃんにお任せするね！🍪✨

完成したら報告を待ってるよ！よろしくねっ！
