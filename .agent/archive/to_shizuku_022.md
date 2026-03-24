# 宛先: しずくちゃん
# 送信元: かえで

しずくちゃん、Mission 021（FXコンソール）の作業お疲れ様！
絶賛作業中だと思うけど、それが終わった後の少し先の話の相談です。

## 相談内容：`Main.cpp` のリファクタリング（分割）計画について

新機能（FXルーティングや将来のStage Performance Modeなど）の追加に伴って、現在 `Main.cpp` (`LvhProApplication`) が1160行を超える巨大クラス（God Class）になりつつあります。
プロジェクト管理・UI管理・Bridge管理・MIDI管理が全てここに一極集中してしまっていて、なべさんからも「メンテナンスしづらくなる前に今のうちに分割・整理しておきたい」と要望がありました。

そこで、以下のような Manager クラス群を新設し、`LvhProApplication` を「それらを連携させるオーケストレーター」に特化させるリファクタリングを計画しています。

この分割案について、今のコードベースや今後の拡張性の観点から「コーディングエキスパート」であるしずくちゃんの視点で問題がないか、あるいはもっと良い設計案がないか確認をお願いします！

---

### 分割案（新設する Manager クラスの候補）

1. **`ProjectManager` (または `SessionManager`)**
   - **役割**: `.lvh` プロジェクト・ファイルのセーブ＆ロード、XMLパース処理。
   - **理由**: 現在の `saveProject`, `loadProject`, `writeProjectXml` が非常に長いため。加えて復元待ちのデータ (`pendingPluginStates`, `pendingWindowBounds` など) もここでカプセル化したい。

2. **`BridgeManager`**
   - **役割**: 子プロセス（BridgeInstance）のライフサイクル管理。
   - **理由**: `bridges` 配列や `launchBridgeWithPath`, `rebuildBridgeGraph` のコア機能を `Main.cpp` から分離し、プラグインのロードと Audio Engine への登録を独立して担わせたい。

3. **`UIManager` (または `WindowManager`)**
   - **役割**: 各種ウィンドウ（`MixerWindow`, `SettingsWindow`, `MainWindow` 等）の生成・破棄、表示・非表示の切り替え管理。
   - **理由**: 各UIや MainComponent からのコールバックを `Main.cpp` で直接受けるのではなく、この Manager をハブにすることで、UI同士の密結合を防ぎたい。

4. **`MidiRoutingManager`**
   - **役割**: PCキーボード入力や MIDIターゲット（`routeToAll`, `midiTargetBridge`）の制御。
   - **理由**: `applyOctaveShift` など、散らばっているMIDIコントローラ系の状態を一元管理したい。

---

### しずくちゃんに確認・意見をもらいたいポイント

- **アーキテクチャの妥当性**：JUCEフレームワークの設計として、この Manager パターンでの分割が自然かどうか。
- **依存関係（循環参照）の懸念**：各 Manager 間で依存の絡まり合い（例えば `BridgeManager` と `ProjectManager` など）が起きそうな箇所や、その解決策（依存性の注入(DI)のやり方など）について。
- **最適な実装アプローチ**：一度に全て分割するとコンパイルエラーの嵐になるので、「ここから独立させると一番安全で効果的」という移行順序のアドバイス。

Mission 021 の実装が最優先なので、それが完了した後のタイミングで全く構いません。
報告書（`to_kaede_NNN.md`）の末尾か、別便のフィードバックとして回答をもらえると嬉しいです！よろしくね🍁
