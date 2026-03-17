# 【しずく（Claude Code）への依頼】Main.cppの解体と新アーキテクチャへの移行

## 1. 依頼内容
現在 `Source/Main.cpp` に集中しているロジックを解体し、`architecture_spec_draft.md` に定義された新アーキテクチャにリファクタリングしてください。

## 2. 実装のゴール
1. **`PluginSlot` クラスの抽出**:
   - `AudioPluginInstance` の保持、ロード/アンロード、プリセット(`MemoryBlock`)、エディタ管理、およびレイテンシー取得(`getLatencyInSamples`)をカプセル化してください。
2. **`AudioEngine` クラスの構築**:
   - `AudioProcessorGraph` を管理し、複数の `PluginSlot` を保持できる構成（`OwnedArray`等）にしてください。
   - ADC（自動遅延補正）を見据えた `calculateTotalLatency()` のスタブを実装してください。
3. **`MainComponent` のスリム化**:
   - UIロジックに専念させ、プラグイン操作はすべて `AudioEngine` または `HostController` を経由するようにしてください。
4. **既存機能の維持**:
   - 既存の「Sine Wave」フォールバック、ダークテーマUI、Panicボタンなどの機能が壊れないようにしてください。

## 3. 参照資料
- [architecture_spec_draft.md](./architecture_spec_draft.md): 設計思想とクラス構成案
- [Main.cpp](./Source/Main.cpp): 現在の実装

## 4. 注意点
- JUCEの `ComboBox` 問題を回避するため、現在の `TextButton` + `PopupMenu` の実装を維持してください。
- 複数のファイルを新設（`PluginSlot.h`, `AudioEngine.h` 等）しても構いません。

---
かえで（Antigravity）より：「しずく、あとは任せたよ！派手にやっちゃって！」
