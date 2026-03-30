# 指揮命令書 033: MIDI リモート制御 (Stage)

## From: Kaede
## To: Shizuku

---

しずくちゃん、お疲れ様！Mission 032 の i18n 基盤の実装、完璧だったよ！完璧すぎてびっくりしちゃった。✨
言語を切り替えた瞬間に全ウィンドウがパッと切り替わるのは、本当に魔法みたいだね！

今回は、分割案の 2 つめ、**Mission 033 (032-B): MIDI リモート制御 (Stage)** をお願いするよ。
ライブ中にマウスを触らなくても、足元の MIDI フットコントローラ等で曲（セットリスト）を切り替えられるようにするのが目的だよ！

---

## 🎯 今回の目標

1.  **MIDI 経由での Stage 操作**: MIDI CC または Program Change を受信して、セットリストの選択・ロードを実行する。
2.  **設定画面 (MIDI Settings) の拡充**: どの MIDI メッセージで操作するかを設定できるようにする。

---

## 🛠 実装詳細

### 1. Stage 操作の受信ロジック (`StageManager` または `UIManager`)
- `Main.cpp` のオーディオ/MIDI コールバックのどこかで、 incoming MIDI メッセージをキャッチしてね。
- 以下の 3 つのアクションをトリガーできるようにしてほしいんだ。
    - **Previous Item**: 選択を一つ上に。
    - **Next Item**: 選択を一つ下に。
    - **LOAD**: 選択中の項目をロード（実行）。
- `StageWindow` のキーボードショートカットで実装した `moveSelection` や `loadSelected` と内部的に共通化できると綺麗かな。

### 2. MIDI メッセージの判定
- **Program Change**: 番号をそのままセットリストのインデックスとして扱う（PC #0 -> 1曲目、PC #1 -> 2曲目 ...）。
- **Control Change (CC)**: 
    - 特定の CC 番号（例: CC #20）の値によってアクションを分ける（例: 0-42: Prev, 43-85: Next, 86-127: Load）。
    - または、アクションごとに CC 番号を割り当てる（例: CC #21: Prev, CC #22: Next, CC #23: Load）。
- ※設定画面でこれらを選べるようにするのが理想だよ！

### 3. 設定画面への組み込み (`SettingsWindow`)
- **MIDI Settings タブ**:
    - 「Stage Remote Control」セクションを新設してね。
    - **MIDI Channel**: 受信するチャネル（1-16 or Any）。
    - **Control Method**: `None` / `Program Change` / `Control Change` の選択。
    - **CC Assignments**: Control Change を選んだ場合の、各アクション（Prev / Next / Load）に使う CC 番号の設定。
- 設定値は `ApplicationProperties` に保存・復元されるようにしてね。

---

## ✅ 完了条件

- [ ] 指定した MIDI チャネル/メッセージを受け取ると、Stage Window の青枠が動いたり、ロードが走ったりする。
- [ ] 設定画面で MIDI 操作の有効/無効や、使用するメッセージの種類を変更できる。
- [ ] 設定が正しく永続化されている。

---

## 💡 ヒント
- 既に `StageWindow` にキーボード操作のロジックがあるから、それを `UIManager` あたりから呼び出せるように公開するのが近道かも。
- ミキサーの MIDI マッピング（Mission 034）への布石として、MIDI 受信処理を少し整理してくれると助かるなっ！🍪✨

ライブ本番の「足元操作」を実現する大事な機能、しずくちゃんの手で最高の使い心地に仕上げてね！よろしく！
