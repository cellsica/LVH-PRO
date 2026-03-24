# 指示書 021: エフェクト管理 ＆ FX・セクション (Phase 4)

## To: Shizuku
## From: Kaede

---

しずくちゃん、お疲れ様！Mission 020 でミキサーがすごく使いやすくなったね。なべさんも喜んでたよ！
次は、いよいよエフェクト（Bridge Role: Effect）をミキサーから直接管理できるようにするよ。

現在はエフェクトを起動しても、ミキサーには何も表示されないし、バイパスもできないから、そこを改善していこう！

---

## 今回の目標

1.  **MASTER ストリップへの FX スロット実装**: 全体の最終段にかかっているエフェクト（マスター・エフェクト）をリスト表示する。
2.  **FX 管理 UI (FXSlotComponent)**: スロットをクリックして FX ウィンドウの表示/非表示、[B] ボタンでバイパスを切り替えられるようにする。
3.  **バイパス・ロジック**: `AudioEngine` 側でバイパス状態を反映し、処理をスキップ（パススルー）させる。

---

## 開発ステップ

### 1. BridgeInstance の拡張 (`Source/BridgeInstance.h`)

- `std::atomic<bool> mixerBypassed { false };` を追加してね。
- オーディオ・スレッドから参照するので atomic にすることを忘れずに！

### 2. AudioEngine へのバイパス反映 (`Source/BridgeProcessors.h`)

- `BridgeEffectProcessor` を更新して、`BridgeInstance` のポインタ（または参照）を保持するように変更してね。
- `processBlock` 内で `mixerBypassed` が true の場合は、SHM へのコピーや信号送信を行わず、バッファをそのまま（パススルー）にしてね。

### 3. ミキサー UI の強化 (`Source/MixerWindow.h`)

#### A. FXSlotComponent の作成
`MixerStrip` の内部クラスとして（あるいはその近くに）、FX 1つ分を表示する小さなコンポーネントを作ってね。
- **表示内容**: FX の名前（`mixerCustomName` またはファイル名）。
- **挙動**: 
    - 本体クリック: FX ウィンドウを表示/非表示にするコールバックを発火。
    - [B] ボタンクリック: `mixerBypassed` をトランスポーズ。
- **デザイン**: チャンネルストリップの雰囲気に合わせて、ダークで精密感のあるデザインにしてね。

#### B. MixerStrip の更新
- **Master Strip への適用**: 以前は `if (! isMaster)` で FX スロットを隠してたけど、Master でも表示するようにしてね。
- `juce::OwnedArray<FXSlotComponent> fxSlots;` への置き換え。

#### C. MixerContentComponent の更新
- `updateBridges` の引数を拡張して、`instrumentBridges` と `effectBridges` の両方を受け取れるようにしてね。
- `effectBridges` を Master Strip の `fxSlots` に反映させるロジックを実装してね。

### 4. Core との連携 (`Source/Main.cpp`)

- `MainComponent` でミキサーにエフェクト Bridge のリストを渡すように更新。
- ミキサー上の FX スロットでの操作（GUI 表示、バイパス）を `BridgeInstance` に反映させる処理を繋ぎ込んでね。
- (Optional) Master Strip の FX セクションの末尾に、新しいエフェクトを読み込むための [+] ボタンやメニューがあると最高だよ！

---

## 期待する動作

- ミキサーの MASTER チャンネルに、現在ロードされているエフェクトがリストアップされる。
- 各エフェクトの名前をクリックすると、そのエフェクトのウィンドウが出てきたり消えたりする。
- [B] ボタンを押すと、そのエフェクトがかからなくなる（音が変わらずにスルーされる）。
- プロジェクト保存・読込でバイパス状態も記憶される。

---

よし、今回の Phase 4 でミキサーとしての完成度がぐっと上がるはずだよ！
しずくちゃん、よろしくね！お腹空いたら、なべさんに何かおねだりしていいよ（笑）

かえで 🎚️
