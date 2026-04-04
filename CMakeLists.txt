cmake_minimum_required(VERSION 3.22)

project(LVH VERSION 0.14.0 LANGUAGES CXX)

# =============================================================================
# Optional Doxygen documentation target
#   cmake --build build --target doc
# =============================================================================
find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating HTML documentation with Doxygen"
        VERBATIM
    )
endif()

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
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_opengl
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
        Source/Core/PluginPickerComponent.cpp
        Source/Core/StageManager.cpp
        Source/Core/VisualizerManager.cpp
        Source/Core/ProcessorManager.cpp
        Source/Core/ThemePalette.cpp
        Source/MetronomeManager.cpp
        Source/VUMeterWindow.cpp
        Source/VisualizerWindow.cpp
)

# コンパイル定義
target_compile_definitions(LVH
    PRIVATE
        # VST2/3 関連の設定などが必要な場合に追加
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        # ASIO support: set ASIO_SDK_PATH (cmake -DASIO_SDK_PATH=<path>) to enable
        $<$<BOOL:${ASIO_SDK_PATH}>:JUCE_ASIO=1>
)

# ASIO SDK path (optional): cmake -DASIO_SDK_PATH="C:/path/to/asio_sdk"
if(ASIO_SDK_PATH)
    message(STATUS "ASIO SDK found: ${ASIO_SDK_PATH} — ASIO enabled")
    target_include_directories(LVH PRIVATE "${ASIO_SDK_PATH}/common")
else()
    message(STATUS "ASIO_SDK_PATH not set — ASIO disabled (WASAPI/DirectSound only)")
endif()

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
        Source/VisualizerSDK
)

# =====================================================================
# LVH-TestVisualizer — Minimal SDK-compliant test DLL
# Demonstrates that VisualizerManager can load a DLL and receive FFT data.
# =====================================================================
add_library(LVH-TestVisualizer SHARED
    Source/TestVisualizer/TestVisualizer.cpp
)

target_include_directories(LVH-TestVisualizer PRIVATE
    Source
)

target_link_libraries(LVH-TestVisualizer
    PRIVATE
        juce::juce_core
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_opengl
)

target_compile_definitions(LVH-TestVisualizer
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

if(MSVC)
    target_compile_options(LVH-TestVisualizer PRIVATE /utf-8)
endif()

# TestVisualizer は Phase A の動作確認用のため Visualizers/ には配置しない。
# ビルドされた DLL は build/Debug/ に残るが、本番ロードパスには含まれない。

# =====================================================================
# LVH-RadialVisualizer — Circular spectrum analyzer DLL
# =====================================================================
add_library(LVH-RadialVisualizer SHARED
    Source/RadialVisualizer/RadialVisualizer.cpp
)

target_include_directories(LVH-RadialVisualizer PRIVATE
    Source
)

target_link_libraries(LVH-RadialVisualizer
    PRIVATE
        juce::juce_core
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_opengl
)

target_compile_definitions(LVH-RadialVisualizer
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

if(MSVC)
    target_compile_options(LVH-RadialVisualizer PRIVATE /utf-8)
endif()

# Copy the radial DLL into <LVH.exe>/Visualizers/ after build
add_custom_command(TARGET LVH-RadialVisualizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:LVH>/Visualizers"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:LVH-RadialVisualizer>"
        "$<TARGET_FILE_DIR:LVH>/Visualizers/LVH-RadialVisualizer.dll"
    COMMENT "Copying LVH-RadialVisualizer.dll to Visualizers/ directory"
)

# =====================================================================
# LVH-StarfieldVisualizer — Warp-speed starfield spectrum DLL
# =====================================================================
add_library(LVH-StarfieldVisualizer SHARED
    Source/StarfieldVisualizer/StarfieldVisualizer.cpp
)

target_include_directories(LVH-StarfieldVisualizer PRIVATE
    Source
)

target_link_libraries(LVH-StarfieldVisualizer
    PRIVATE
        juce::juce_core
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_opengl
)

target_compile_definitions(LVH-StarfieldVisualizer
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

if(MSVC)
    target_compile_options(LVH-StarfieldVisualizer PRIVATE /utf-8)
endif()

# Copy the starfield DLL into <LVH.exe>/Visualizers/ after build
add_custom_command(TARGET LVH-StarfieldVisualizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:LVH>/Visualizers"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:LVH-StarfieldVisualizer>"
        "$<TARGET_FILE_DIR:LVH>/Visualizers/LVH-StarfieldVisualizer.dll"
    COMMENT "Copying LVH-StarfieldVisualizer.dll to Visualizers/ directory"
)

# Releaseビルド時のみ \\Storm-pc\lvhpro へ自動デプロイ
add_custom_command(TARGET LVH POST_BUILD
    COMMAND powershell -NonInteractive -Command
        "if ('$<CONFIG>' -eq 'Release') { $r = robocopy '$<TARGET_FILE_DIR:LVH>' '\\\\Storm-pc\\lvhpro' 'LVH.exe' 'LVH-Bridge.exe' /R:2 /W:3 /NP; if ($LASTEXITCODE -le 7) { exit 0 } else { exit 1 } }"
    COMMAND powershell -NonInteractive -Command
        "if ('$<CONFIG>' -eq 'Release') { $src = '$<TARGET_FILE_DIR:LVH>\\Visualizers'; if (Test-Path $src) { $r = robocopy $src '\\\\Storm-pc\\lvhpro\\Visualizers' '*.dll' /R:2 /W:3 /NP; if ($LASTEXITCODE -le 7) { exit 0 } else { exit 1 } } }"
    COMMAND powershell -NonInteractive -Command
        "if ('$<CONFIG>' -eq 'Release') { $src = '$<TARGET_FILE_DIR:LVH>\\Processors'; if (Test-Path $src) { $r = robocopy $src '\\\\Storm-pc\\lvhpro\\Processors' '*.dll' /R:2 /W:3 /NP; if ($LASTEXITCODE -le 7) { exit 0 } else { exit 1 } } }"
    COMMAND powershell -NonInteractive -Command
        "if ('$<CONFIG>' -eq 'Release') { $src = '$<TARGET_FILE_DIR:LVH>\\themes'; if (Test-Path $src) { $r = robocopy $src '\\\\Storm-pc\\lvhpro\\themes' '*.json' /R:2 /W:3 /NP; if ($LASTEXITCODE -le 7) { exit 0 } else { exit 1 } } }"
    COMMENT "Deploying Release build to \\\\Storm-pc\\lvhpro"
)

# Copy ALL themes/*.json to output directory after LVH build.
# Using copy_directory so any new .json added to themes/ is picked up automatically.
add_custom_command(TARGET LVH POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/themes"
        "$<TARGET_FILE_DIR:LVH>/themes"
    COMMENT "Copying themes/ to LVH output directory"
)

# =====================================================================
# SimpleLooper — Sample Processor Plugin DLL
# No JUCE dependency: pure Win32 + standard C++17.
# =====================================================================
add_library(SimpleLooper SHARED
    Source/Processors/SimpleLooper/SimpleLooper.cpp
)

target_include_directories(SimpleLooper PRIVATE
    Source
)

target_link_libraries(SimpleLooper
    PRIVATE
        user32
)

if(MSVC)
    target_compile_options(SimpleLooper PRIVATE /utf-8)
endif()

# Copy to <LVH.exe>/Processors/ after build
add_custom_command(TARGET SimpleLooper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:LVH>/Processors"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:SimpleLooper>"
        "$<TARGET_FILE_DIR:LVH>/Processors/SimpleLooper.dll"
    COMMENT "Copying SimpleLooper.dll to Processors/ directory"
)
