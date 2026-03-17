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
- [ ] Bridge から Core への音声データ返却
- [ ] 複数インスタンスのブリッジ管理
- [ ] 独立ウィンドウ管理: 子プロセス側のウィンドウを親プロセス側で制御する仕組み。

## 直近の作業ログ 📝
- [2026-03-17] かえで：Bridge プロトタイプの最小構成ソースコードを実装。再起動後にビルドを行う準備が完了。
- [2026-03-17] しずく：IPC基盤 (IPCManager.h/cpp) を実装。Named Pipe による Core↔Bridge ハンドシェイク・MIDI転送・SharedAudioLayout定義を完了。