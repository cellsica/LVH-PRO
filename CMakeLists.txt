cmake_minimum_required(VERSION 3.22)

project(LVH VERSION 0.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE を GitHub から FetchContent で取得
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12 # 使用する JUCE のバージョンに合わせて変更してください
)
FetchContent_MakeAvailable(JUCE)

# プロジェクトターゲットの追加 (GUI アプリケーション)
juce_add_gui_app(LVH
    PRODUCT_NAME "LVH"
    BUNDLE_ID    "com.cellsica.lvh"
    ICON_BIG     images/LVH_desktop_icon.png
    ICON_SMALL   images/LVH_desktop_icon.png
)

# 画像などのバイナリリソースをコードに埋め込む
juce_add_binary_data(LVH_BinaryData
    SOURCES
        images/LVH_LOGO.png
)

# 必要な JUCE モジュールのリンク
target_link_libraries(LVH
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        LVH_BinaryData
)

# ソースファイルの追加 (後ほど Source フォルダを作成)
target_sources(LVH
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/LevelMeter.cpp
        Source/InfoMonitorPanel.cpp
        Source/SystemLogPanel.cpp
        Source/UiComponents.cpp
        Source/SettingsWindow.cpp
        Source/IPCManager.cpp
        Source/BridgeInstance.cpp
        Source/Core/MidiRoutingManager.cpp
        Source/Core/ProjectSerializer.cpp
        Source/Core/BridgeManager.cpp
        Source/Core/UIManager.cpp
        Source/Core/StageManager.cpp
        Source/MetronomeManager.cpp
        Source/VUMeterWindow.cpp
)

# コンパイル定義
target_compile_definitions(LVH
    PRIVATE
        # VST2/3 関連の設定などが必要な場合に追加
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
)

# MSVC での文字コード問題を解決するために /utf-8 を追加
if(MSVC)
    target_compile_options(LVH PRIVATE /utf-8)
endif()

# JuceHeader.h を生成するように指示
juce_generate_juce_header(LVH)

# =====================================================================
# LVH-Bridge ターゲットの追加 (プラグインホスト用子プロセス)
# =====================================================================
juce_add_gui_app(LVH-Bridge
    PRODUCT_NAME "LVH-Bridge"
    BUNDLE_ID    "com.cellsica.lvh-bridge"
)

target_sources(LVH-Bridge
    PRIVATE
        Source/BridgeMain.cpp
        Source/IPCManager.cpp
)

target_link_libraries(LVH-Bridge
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
)

target_compile_definitions(LVH-Bridge
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
)

if(MSVC)
    target_compile_options(LVH-Bridge PRIVATE /utf-8)
endif()

juce_generate_juce_header(LVH-Bridge)

target_include_directories(LVH-Bridge
    PRIVATE
        Source
        Source/Core
)

# LVH-Bridge のビルド後、LVH の出力ディレクトリへ自動コピー
# (LVH は currentExecutableFile と同じフォルダの LVH-Bridge.exe を起動するため)
add_custom_command(TARGET LVH-Bridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:LVH-Bridge>"
        "$<TARGET_FILE_DIR:LVH>/LVH-Bridge.exe"
    COMMENT "Copying LVH-Bridge.exe to LVH output directory"
)

# インクルードディレクトリの設定 (LVH)
target_include_directories(LVH
    PRIVATE
        Source
        Source/Core
)
