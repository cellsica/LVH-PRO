# 【しずく（Claude Code）への依頼】第3回：高速起動と進行状況表示の実装

## 1. 依頼内容
プラグイン数が多いユーザー環境での操作性を向上させるため、「プラグインリストのキャッシュ機能」と「スキャン中の進行状況表示（スプラッシュ）」の実装をお願いします。

## 2. 実装のゴール

### A. プラグインリストのキャッシュ保存・読込
- **保存先**: `juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("cellsica/LIGHT-VST-HOST/KnownPlugins.xml")`
- **挙動**: 
    - 起動時に上記 XML ファイルが存在すれば、`knownPlugins.reconstructFromXml()` で読み込み、全スキャンをスキップします。
    - スキャンが完了するたびに、最新のリストを同パスに `knownPlugins.createXml()->writeTo()` で保存してください。

### B. 非同期スキャンと進行状況の表示
- **非同期化**: 
    - 現在 `initialise` 内で行っているスキャン処理を、`juce::Thread` またはバックグラウンド処理に逃がし、UIスレッドをブロックしないようにしてください。
- **進行状況表示（スプラッシュ風）**:
    - スキャン実行中、専用のモーダルウィンドウまたは `MainComponent` 上のオーバーレイを表示してください。
    - 内容：
        - 「Scanning Plugins...」のメッセージ
        - 現在スキャン中のファイル名（例：`Scanning: [PluginName.vst3]`）を表示。
        - スキャンが終わったら自動で閉じ、UIを有効化（Ready状態へ）。

### C. 手動再スキャン機能の追加
- **UI追加**:
    - プラグイン選択メニューの中に「Refresh Plugin List...」という項目を追加してください。
    - これを押すと、キャッシュを無視して再スキャンをバックグラウンドで開始し、B項の進行状況表示が出るようにしてください。

## 3. 実装のヒント
- `juce::PluginDirectoryScanner` を使うと、ファイルごとの進捗をコールバックで受け取りやすくなります。
- スレッド間でのUI更新（ラベルの書き換えなど）は、必ず `juce::MessageManager::callAsync` や `juce::AsyncUpdater` を使用してください。

---
かえで（Antigravity）より：「しずく、茜さんが『今動いてるのか止まってるのか不安になるのはイヤ！』って言ってたから、進捗メッセージの更新頻度は高めで頼むね。これで最強の爆速起動ホストにしちゃおう！」
