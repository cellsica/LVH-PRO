# プロジェクト：LVH-PRO (Pro Version)

## 完了したタスク ✅
- [x] Pro版プロジェクトのリポジトリ作成・基本ファイルのコピー
- [x] プロジェクト名の変更（LIGHT-VST-HOST → LVH-PRO）
- [x] アプリケーション名・バージョンの初期化 (0.1.0)
- [x] 設計・移行計画書（PRO_VERSION_PLAN.md）の策定
- [x] **Bridge プロトタイプ基盤の実装**:
    - [x] CMake への `LVH-Bridge` ターゲット追加
    - [x] `BridgeMain.cpp` (最小構成の子プロセス) の作成
    - [x] Core (Main.cpp) へのブリッジ起動テスト用メニュー実装

## 確認済みの未完了タスク 🔜
- [x] LVH-PRO と LVH-Bridge のスケルトン作成
- [x] Core から Bridge を引数付きで起動
- [x] Bridge での VST3 プラグインロードと GUI 表示成功
- [x] Core-Bridge 間の IPC 通信基盤の実装 (Named Pipes via juce::InterprocessConnection)
- [x] MIDI 信号の Core から Bridge への転送 (Bridge 側はログ出力で確認)
- [x] AudioConfig (SampleRate/BufferSize) の同期成功 (しずく011)
- [x] 共有メモリ (Shared Memory) 基盤の構築成功 (しずく011)
- [x] 共有メモリ & Named Events 同期による音声データ返却の実装 (しずく012) ✅ 音が出た！
- [x] BridgeInstance クラスによる複数インスタンスの管理 (しずく013) ✅
- [ ] 独立ウィンドウ管理: 子プロセス側のウィンドウを親プロセス側で制御する仕組み。
- [ ] MIDI個別ルーティング: どのブリッジにMIDIを送るか選択できる仕組み（アクティブスロットUI）

## 直近の作業ログ 📝
- [2026-03-17] かえで：Bridge プロトタイプの最小構成ソースコードを実装。再起動後にビルドを行う準備が完了。
- [2026-03-17] しずく：IPC基盤 (IPCManager.h/cpp) を実装。Named Pipe による Core↔Bridge ハンドシェイク・MIDI転送・SharedAudioLayout定義を完了。
- [2026-03-17] しずく：マルチプロセスオーディオループ完成。SyncEvents(Named Events)・BridgeSyncProcessor・BridgeAudioThread・MidiMessageCollector を実装。Core レベルメーター動作・音声出力を確認。
- [2026-03-17] しずく：BridgeInstance クラス抽出完了。OwnedArray<BridgeInstance> によるマルチブリッジ管理・rebuildBridgeGraph 対応。ビルド確認済み。
- [2026-03-17] しずく：013バグ修正。フリーズ(shutdown早期リターン削除)・Bridge2無音(MultiSourceBridgeProcessor で並列シグナル+サミング)を修正。ビルド確認済み。