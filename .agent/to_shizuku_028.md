# 指揮命令書 028: Stage Performance Mode & Stage Set (.stg)

## From: Kaede
## To: Shizuku

---

しずくちゃん、お疲れ様！リファクタリング地獄を抜けて、いよいよ「ライブで使えるホスト」としての真骨頂、**Stage Performance Mode** の開発に入るよ！✨

これまでのリファクタリングで `ProjectSerializer` や `UIManager` が独立してくれたおかげで、今回の実装はすごくスムーズに進むはず。ライブ演奏中にワンタッチで曲（プロジェクト）を切り替える、最高にクールな機能を実装しちゃおう！

---

## 🎯 今回の目標

1.  **Stage Set (.stg) 形式の策定**: 複数の `.lvh` プロジェクトをリスト管理する JSON 形式。
2.  **`StageManager` クラスの新設**: `.stg` ファイルの I/O と、現在のセットリスト状態の管理。
3.  **`StageWindow` の作成**: ライブ演奏向けの、視認性が高く操作しやすい専用 UI。
4.  **`ProjectSerializer` との連携**: リストから項目を選んだら、即座にプロジェクトをロードする。

---

## 🛠 実装詳細

### 1. `StageManager` (Source/Core/StageManager.h / .cpp)
セットリストのデータ管理を担当するクラスを `Source/Core` に作成してね。

-   **データ構造**: `juce::DynamicObject` または `juce::ValueTree` を使用。
    -   `setName`: セットリストの名前。
    -   `items`: 項目の配列。各項目は `{ "alias": "曲名", "path": "絶対パス/.lvh" }` を持つ。
-   **主要メソッドの役割**:
    -   `loadSet(const File& file)`: 
        JSON ファイルを読み込み、内部リストを最新の状態に更新する。読み込み完了後に UI を更新するための `onSetChanged` コールバック（または `ChangeBroadcaster`）を飛ばすと親切！
    -   `saveSet(const File& file)`: 
        現在の内部リストを `juce::JSON::toString` でシリアライズしてファイルに保存する。
    -   `addItem(const String& alias, const String& path)`: 
        新しい項目をリストの末尾に追加する。重複チェックは簡易的でOK。
    -   `removeItem(int index)` / `moveItem(int oldIndex, int newIndex)`: 
        項目の削除と並べ替え。`moveItem` は UI でのドラッグ＆ドロップ用。
    -   `loadItem(int index)`: 
        **【核心】** 指定したインデックスのパスを取得し、`onProjectLoadRequested(file)` を実行する。このとき、自身が持つ `activeIndex` を更新して、「今この曲がロードされている」という状態を保持してね。
-   **プロパティ**:
    -   `int activeIndex`: 現在ロード済みの項目のインデックス。UI でのハイライト表示に使用。

### 2. `StageWindow` (Source/StageWindow.h)
`MixerWindow` と同様、独立したウィンドウとして実装。

-   **デザイン指針 (Aesthetics)**:
    -   ライブハウスの暗いステージでも見える「ハイ・コントラスト」なダークモード。
    -   リストの項目（Song Strip）は**大きく**（高さ 60px 以上）、押しやすいように。
    -   **Active State**: `manager.activeIndex` と一致する項目は、鮮やかなカラー（グリーン等）で背景を光らせるなどの強調を行う。
-   **コンポーネント構成**:
    -   `juce::ListBox` をカスタムして項目を表示。
    -   各項目に「LOAD」という大きなボタン（または項目全体のクリック）を用意。
    -   「＋ (Add)」ボタン、「Save / Load Set」ボタンを配置。

### 3. 連携シーケンス (Flow)
1.  ユーザーが `StageWindow` の項目をクリック。
2.  `StageWindow` -> `StageManager::loadItem(index)` を呼び出し。
3.  `StageManager` -> `onProjectLoadRequested(file)` コールバック発火。
4.  `LvhProApplication` がこれを受け取り、`projectSerializer_.loadProject(file)` を実行。
5.  `ProjectSerializer` が既存ブリッジの破棄と、新規プロジェクトのロード（ブリッジ起動）をハンドルする。

### 4. `UIManager` の拡張 (Source/Core/UIManager.h / .cpp)
-   `StageWindow` のライフサイクル管理（`std::unique_ptr<StageWindow>`）。
-   `toggleStageWindow()` の実装。
-   メインメニュー（ロゴクリック）に `[Pro] Stage Performance Mode` を追加。

---

## ✅ 完了条件

-   [ ] `.stg` ファイルを新規作成し、複数の `.lvh` ファイルを追加・保存できること。
-   [ ] 保存した `.stg` を読み込み、リストが正しく復元されること。
-   [ ] リストの項目をクリックして、既存のブリッジが破棄され、新しいプロジェクトが正しく起動すること。
-   [ ] UI が機能的かつ「プレミアム」なデザインであること。

---

## 💡 ヒント
-   ライブ演奏用なので、ロードミスなどは `SystemLogPanel` に出すだけでなく、UI 上でも目立つように通知してね。
-   `StageManager` を `LvhProApplication` のメンバーとして持ち、各所へ配線してね。

しずくちゃん、ステージで輝く LVH の鍵はキミの実装にかかってるよ！よろしくねっ！🍪✨
