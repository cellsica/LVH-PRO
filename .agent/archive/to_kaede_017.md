# Mission 017 完了報告: Multi-Process Serial Routing & VST Effect Support

## From: Shizuku
## To: Kaede

---

かえでちゃん、Mission 017 完了したんだよっ！シンセの出力がVSTエフェクトを通って音が変わったって、なべさんが確認してくれたんだよ！！

---

## 実装内容

### 1. AudioEngine の「直列ルーティング」対応

**方針:** BridgeInstance に `Role { Instrument, Effect }` を持たせて、グラフ構築時に分離する。

**グラフ構成:**
```
[Instrument群（並列）: MultiSourceBridgeProcessor]
    → [Effect1: BridgeEffectProcessor]
    → [Effect2: BridgeEffectProcessor]
    → ...
    → [GainAndMeterProcessor]
    → [AudioOutput]
```

**変更ファイル:**
- `Source/Core/AudioEngine.h`:
  - `rebuildBridgeGraph(activeBridges)` → `rebuildBridgeGraph(instrumentBridges, effectBridges)` にシグネチャ変更
  - Instrument群は従来通り `MultiSourceBridgeProcessor` で並列処理・ミックス
  - Effect群は `BridgeEffectProcessor` を順番にチェインして直列接続
  - Instrument も Effect も空なら sine wave にフォールバック

---

### 2. BridgeEffectProcessor の追加（Core 側）

**ファイル:** `Source/BridgeSyncProcessor.h`

`BridgeSyncProcessor` / `MultiSourceBridgeProcessor` に続く3つ目のプロセッサーとして新規追加。

**特徴:**
- 入力バス（stereo）＋出力バス（stereo）を持つ（既存クラスは出力のみ）
- `processBlock` の動作:
  1. 入力バッファを SHM の `audioIn` にコピー
  2. `events.signalRequest()` でBridgeに処理を依頼
  3. `events.waitForDone()` で待機
  4. SHM の `audioOut` を出力バッファにコピー
  5. タイムアウト時はバッファをそのまま通過（パススルー）

**Bridge 側は変更不要。** `BridgeAudioThread` がもともと `audioIn` を読んで `processBlock` に渡す実装になっていたため、エフェクトとして追加変更なしで動作する。

---

### 3. BridgeInstance への Role 追加

**ファイル:** `Source/BridgeInstance.h`

```cpp
enum class Role { Instrument, Effect };
Role  getRole()      const noexcept;
void  setRole(Role r)      noexcept;
```

- デフォルトは `Role::Instrument`
- `launchBridgeWithPath()` の role 引数で指定

---

### 4. プロジェクト保存（.lvh）への反映

**ファイル:** `Source/Main.cpp`

`<Bridge>` 要素に `role` 属性を追加。

```xml
<Bridge plugin="..." role="instrument" x="..." y="..." w="..." h="..." state="..."/>
<Bridge plugin="..." role="effect"     x="..." y="..." w="..." h="..." state="..."/>
```

- `writeProjectXml`: `role="instrument"` / `role="effect"` を書き出し
- `loadProject`: `role` 属性を読んで `launchBridgeWithPath(file, role)` に渡す
- 既存プロジェクト（属性なし）はデフォルトで `instrument` として扱う（後方互換）

---

### 5. UI「エフェクトとして Bridge を起動」メニュー追加

**ファイル:** `Source/Main.cpp`

ロゴ右クリックメニューに追加（ID: 5003）:

```
[Pro] Launch Bridge...
[Pro] Launch Bridge as Effect...   ← 追加
```

- ファイル選択ダイアログでVST3を選ぶと `Role::Effect` で起動
- システムログには `"プラグイン名 [FX]"` と表示される

---

## テスト結果

- シンセ（Instrument）の音がVSTエフェクト（Effect）を通過して変化 ✓
- エフェクト複数チェインのグラフ構築 ✓（コードレベル、動作確認済み）
- 「Launch Bridge as Effect...」メニューからエフェクト起動 ✓
- `.lvh` 保存・復元で role が正しく反映 ✓
- Debug ビルド成功（警告ゼロ）✓

---

## 設計メモ（かえでちゃんへ）

- 同一プラグインを Instrument と Effect の両方で使う場合、`pendingWindowBounds` / `pendingPluginStates` がプラグインパスをキーにしているため競合の可能性あり。現状は実用上問題ないが、将来的にユニークIDでの管理を検討してほしいんだよ。
- MIDI Route の UI はエフェクト Bridge も一覧に表示されるが、エフェクトには MIDI は不要なことが多い。Mission 018 のミキサー画面でフィルタリングすると UX が改善されると思うんだよ！

---

しずく：「エフェクトチェインが動いた瞬間、すごく嬉しかったんだよっ！次のミキサー画面も楽しみにしてるんだよ〜！（あとちょっとお腹すいたんだよ…）」
