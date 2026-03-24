# 【しずく（Claude Code）への依頼】第2回：安定性向上とUI拡張

## 1. 依頼内容
なべさんと茜さんからのフィードバックに基づき、パニックボタンの不具合修正と、仮想キーボード（Virtual Keyboard）周りのUI機能追加をお願いします。

## 2. 実装のゴール

### A. パニックボタン（MIDI All Notes Off）の確実な動作
- **事象**: リリースの長い音を発音中に PANIC を押しても音が止まらない場合がある。
- **改修**:
    - `AudioEngine::allNotesOff()` 内で、`keyboardState.allNotesOff()` の呼び出しに加え、Midiバッファ経由で各チャンネルに確実に `AllNotesOff (cc123)` および `AllSoundOff (cc120)` が届くように見直してください。
    - また、`AudioProcessorPlayer` だけでなく、現在ロードされている各プラグイン（`PluginSlot`内のインスタンス）に対しても個別に MIDI メッセージを直接流し込むような、より堅牢な実装を検討してください。

### B. 仮想キーボードの表示/非表示切り替え
- **UI変更**:
    - ツールバー (`MainComponent`の上部エリア) に、キーボードのアイコン、または「KBD」などの文字のボタンを追加してください。
    - このボタンを押すことで、画面下部の `keyboardComponent` の表示/非表示をトグルできるようにしてください。
- **リサイズ連動**:
    - 非表示にした際は、ウィンドウの高さからキーボード分 (100px) を差し引くようにリサイズしてください。

### C. 仮想キーボードのサイズ変更（スプリッター）
- **UI変更**:
    - プラグイン表示エリア (`Viewport`) と `keyboardComponent` の間に、`juce::StretchableLayoutResizerBar` 等を用いて、ユーザーがマウスドラッグで境界を動かせる仕組みを導入してください。
- **連動**:
    - ドラッグに合わせてキーボードの高さが変わり、それに伴ってウィンドウ全体の高さも追従するようにしてください。

## 3. 参照資料
- [Main.cpp](./Source/Main.cpp): UIロジック
- [AudioEngine.h](./Source/AudioEngine.h): パニックボタン（allNotesOff）の実装箇所
- [project_manifesto.md](./.agent/doc/project_manifesto.md): 茜さんの「安定感」へのこだわりを再確認

---
かえで（Antigravity）より：「しずく、茜さんが『安定感が命！』って言ってたから、パニックボタンは絶対止まるように頼むね。UIも使いやすくしちゃって！」
