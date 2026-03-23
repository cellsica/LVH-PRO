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
- [x] Core から Bridge を引数付きで起動
- [x] Bridge での VST3 プラグインロードと GUI 表示成功
- [x] Core-Bridge 間の IPC 通信基盤の実装 (Named Pipes via juce::InterprocessConnection)
- [x] MIDI 信号の Core から Bridge への転送 (Bridge 側はログ出力で確認)
- [x] AudioConfig (SampleRate/BufferSize) の同期 success (しずく011)
- [x] 共有メモリ (Shared Memory) 基盤의 構築成功 (しずく011)
- [x] 共有メモリ & Named Events 同期による音声データ返却の実装 (しずく012) ✅ 音が出た！
- [x] BridgeInstance クラスによる複数インスタンスの管理 (しずく013) ✅
- [x] **IPC 切断検知バグ修正 & ハートビート実装**: Bridge 切断時の再接続・フリーズ問題を解消 (しずく014)
- [x] **Core UI リデザイン**: SystemLogPanel 追加、レイアウト刷新 (しずく014)
- [x] **Select Instruments サブメニュー**: プラグインキャッシュからのロード機能 (しずく014)
- [x] **Mission 015**: PCキーボードのオクターブ修正 & オクターブ切り替え機能の実装 (しずく015)
    - [x] Middle C 表記修正 (C4=MIDI 72 に統一)
    - [x] ツールバーにオクターブ Up/Down ボタンと表示ラベル追加
    - [x] `[` / `]` によるショートカット切り替え実装
    - [x] オクターブ切り替え時の音止まり（ノートオフ）バグ対策
- [x] **Mission 016**: プロジェクト保存・読込（.lvh）＆設定画面の強化 (しずく016)
    - [x] 各 Bridge の状態（プラグイン、音色、ウィンドウ位置）を XML 保存・復元
    - [x] MIDI ルーティング、トランスポーズ、オクターブ設定の保存
    - [x] IPC 拡張: WindowPos, RequestState, StateData, SetState 実装
    - [x] Plugin Paths 設定ページの ListBox 実装と自動再スキャン対応
- [x] **Mission 017**: マルチプロセス・ルーティング ＆ エフェクト(FX)統合基盤の構築 (しずく017)
- [x] **Mission 018**: 独立ウィンドウ型「ミキサー・コンソール」の構築 (Phase 1)
    - [x] 独立ウィンドウ `MixerWindow` の実装
    - [x] Bridge リストからのストリップ動的生成
    - [x] LED メーターの UI デザイン (Icons::led)



- [x] **Mission 019**: ミキシング操作 ＆ リアルタイム・メーター (Phase 2) ✅ (しずく019)
    - [x] 音量・パン、ソロ・ミュートのロジック実装。
    - [x] LED メーターのリアルタイム表示アニメーション。
    - [x] Core ツールバー ↔ Mixer MASTER の双方向同期。




## 確認済みの未完了タスク 🔜
- [x] **Mission 020**: ミキサー UI 強化 ＆ カスタマイズ (Phase 3) ✅ (しずく020)
- [x] **Mission 021**: エフェクト管理 ＆ FX・セクション (Phase 4) ✅ (しずく021)
- [x] **Mission 022**: リファクタリング設計・フェーズ移行計画の策定 ✅ (022)
- [x] **Mission 023**: リファクタリング Phase A (MidiRoutingManager の独立化) ✅ (しずく023)
- [x] **Mission 024**: リファクタリング Phase B (ProjectSerializer の抽出) ✅ (しずく024)
- [x] **Mission 025**: リファクタリング Phase C (BridgeManager の抽出) ✅ (しずく025)
- [x] **Mission 026**: リファクタリング Phase D (UIManager の抽出) ✅ (しずく026)
- [x] **Mission 027**: ミキサー主導のプラグイン追加と UI の整理 ✅ (しずく027)
- [ ] **Mission 028**: Stage Performance Mode & Stage Set (.stg) 🔜



## 直近の作業ログ 📝
- [2026-03-17] かえで：Bridge プロトタイプの最小構成ソースコードを実装。
- [2026-03-17] しずく：IPC基盤 (IPCManager.h/cpp) を実装。
- [2026-03-17] しずく：マルチプロセスオーディオループ完成。
- [2026-03-19] しずく：Mission 014 完了。
- [2026-03-19] かえで：Mission 015 指示書作成。
- [2026-03-19] しずく：Mission 015 完了。オクターブ切り替えと Middle C 表記修正。
- [2026-03-19] かえで：Mission 016 指示書作成（ウィンドウ位置記憶のリクエスト追加）。
- [2026-03-19] しずく：Mission 016 完了。プロジェクト保存（音色・位置含む）・読込、設定パス対応。
- [2026-03-20] しずく：Mission 017 完了。マルチプロセス・エフェクトチェインが動作！
- [2026-03-20] かえで：Mission 018 指示書を「Phase 1：基礎の構築」に分割して再送。しずくちゃんにバトンタッチ。
- [2026-03-20] しずく：Mission 018 (Phase 1) 完了！動的ミキサーウィンドウが動作。
- [2026-03-21] かえで：Mission 019 (Phase 2) 指示書作成。
- [2026-03-21] しずく：Mission 019 (Phase 2) 完了！フェーダー連動、メーター、保存/復元が完璧。
- [2026-03-21] かえで：Solo ロジックを複数同時指定可能に微調整（ Chief Architect check ）。
- [2026-03-22] しずく：Mission 020 (Phase 3) 完了！目盛り、CH編集、サムデザインを刷新。
- [2026-03-21] しずく：Mission 020 (Phase 3) 完了！目盛り・名前編集・カラー選択・■サム・フェーダースリム化。
- [2026-03-23] かえで：Mission 021 (Phase 4) 指示書作成。しずくちゃんにバトンタッチ。
- [2026-03-23] しずく：Mission 021 (Phase 4) 完了！FXバイパス機能とMIDI INの保存に対応。
- [2026-03-23] かえで：Main.cppのリファクタリング（分割）計画案を作成し、しずくにレビュー依頼。
- [2026-03-23] しずく：Mission 023 (Phase A) 完了！MidiRoutingManagerの抽出に成功。
- [2026-03-23] かえで：Mission 024 (Phase B) 指示書作成。
- [2026-03-23] しずく：Mission 024 (Phase B) 完了！ProjectSerializerの抽出。250行削減！
- [2026-03-23] かえで：Mission 025 (Phase C) 指示書作成。
- [2026-03-23] しずく：Mission 025 (Phase C) 完了！BridgeManagerの抽出。依存関係をB案で最適化。
- [2026-03-23] かえで：Mission 026 (Phase D) 指示書作成。
- [2026-03-23] しずく：Mission 026 (Phase D) 完了！UIManagerの抽出。God Class解体完了！
- [2026-03-23] かえで：Mission 027 (Mixer-Driven Plugin Loading & UI Refinement) 指示書作成。
- [2026-03-23] しずく：Mission 027 完了！ミキサーからのFX追加フロー完成。Ver 0.2.1-alphaリリース。
- [2026-03-23] かえで：Stage Performance Mode (Mission 028) の構築準備開始。🔜