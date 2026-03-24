# 報告書 027: ミキサー主導のプラグイン追加と UI の洗練

## From: Shizuku
## To: Kaede

---

かえでちゃん、Mission 027 の実装が完了したんだよっ！✨

---

## ✅ 実施内容

### 1. `FXSlotComponent` の強化 (`Source/MixerWindow.h`)

- `bridge == nullptr` のプレースホルダー時に `"+"` を**中央揃えで表示**するように変更したんだよ
- `std::function<void()> onAddFx` コールバックを追加
- `mouseDown()` を修正: プレースホルダー状態でクリックすると `onAddFx` を発火してリターン。実スロットは従来通り `onToggleWindow` を発火するんだよ

### 2. ミキサー全体の `onAddFx` バブルアップ配線 (`Source/MixerWindow.h`)

コールバックが `FXSlotComponent → MixerStrip → MixerContentComponent → MixerWindow` と正しく伝播する仕組みを実装したんだよ。

- `MixerStrip`: `onAddFx` コールバックを追加。非マスターの4つのスロット（`"+"` 表示）がクリックされると `onAddFx` にバブルアップ
- `MixerContentComponent`: `onAddFx` を追加。インストゥルメントストリップからもバブルアップ
- `MixerWindow`: `onAddFx` を追加。`content->onAddFx` から中継

### 3. マスタースロット常時4表示 (`Source/MixerWindow.h`)

`updateBridges()` で、エフェクトスロットを埋めた後、残りを `"+"` プレースホルダーで補完して常に **4スロット** 表示するようにしたんだよ：

```cpp
const int kMasterFxSlots = 4;
int numPlaceholders = jmax (0, kMasterFxSlots - effectBridges.size());
for (int i = 0; i < numPlaceholders; ++i)
{
    auto* slot = masterStrip->addFxSlot();
    slot->setFxName ("+");
    slot->onAddFx = [this] { if (onAddFx) onAddFx(); };
}
```

### 4. `UIManager` の修正 (`Source/Core/UIManager.cpp` / `.h`)

#### メニュー整理
- `showMainMenu()` から `[Pro] Launch Bridge...` (ID 2) と `[Pro] Launch Bridge as Effect...` (ID 5003) を**削除**したんだよ
- サブメニュー名を `"Select Instruments"` → `"Select Plugins"` に変更

#### 自動ロール判定
`Select Plugins` から起動する際に `PluginDescription::isInstrument` を参照して、インストゥルメントなら `Role::Instrument`、それ以外は `Role::Effect` を自動選択するようにしたんだよ：

```cpp
auto role = desc.isInstrument ? BridgeInstance::Role::Instrument
                              : BridgeInstance::Role::Effect;
```

#### `showPluginPicker(BridgeInstance::Role fixedRole)` の追加
ミキサーの `+` スロット用に、スタンドアローンのプラグイン選択ポップアップを実装したんだよ。ID 100〜 でプラグイン一覧を表示し、選択時は `fixedRole` (常に `Role::Effect`) で `launchBridgeWithPath` を呼ぶんだよ。

#### ミキサー連携
`toggleMixerWindow()` 内で `mixerWindow_->onAddFx` を配線：

```cpp
mixerWindow_->onAddFx = [this] {
    showPluginPicker (BridgeInstance::Role::Effect);
};
```

---

## ✅ ビルド結果

Debug ビルド: **コンパイルエラー・警告なし** ✨

変更ファイル:
- `Source/MixerWindow.h`
- `Source/Core/UIManager.h`
- `Source/Core/UIManager.cpp`

---

## ✅ 完了条件チェック

| 条件 | 状態 |
|------|------|
| ロゴメニューからのファイル選択ブリッジ起動項目が削除された | ✅ |
| リストから選んだ際、isInstrument で自動ロール判定 | ✅ |
| ミキサーの空きスロットの `+` クリックでエフェクト追加 | ✅ |
| マスタースロット常時 4 スロット表示 | ✅ |

---

しずくの魔法、無事に発動できたんだよっ！🍪✨
次のフェーズも張り切って頑張るんだよ！

