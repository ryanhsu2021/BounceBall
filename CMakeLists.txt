cmake_minimum_required(VERSION 3.22)

project(BounceBall VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE
#   默认用 CMake 自动拉取 JUCE 8.0.4（首次 configure 需要联网，约 100MB）。
#   如果本机已经有 JUCE 源码，可以改用本地目录，省去重复下载：
#     cmake -B build -G "Visual Studio 17 2022" -A x64 ^
#           -DBOUNCEBALL_JUCE_DIR=D:/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)

set(BOUNCEBALL_JUCE_DIR "" CACHE PATH "本地 JUCE 源码目录（留空则自动下载）")

if (BOUNCEBALL_JUCE_DIR)
    add_subdirectory("${BOUNCEBALL_JUCE_DIR}" JUCE)
    message(STATUS "BounceBall: 使用本地 JUCE -> ${BOUNCEBALL_JUCE_DIR}")
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
    message(STATUS "BounceBall: 已自动获取 JUCE 8.0.4")
endif()

juce_add_plugin(BounceBall
    COMPANY_NAME              "Bounce Audio"
    IS_SYNTH                  TRUE           # MIDI 生成器：作为乐器加载，内置音源发声
    NEEDS_MIDI_INPUT          TRUE           # Live 要求乐器类 VST3 必须有 MIDI 事件输入总线（内部可忽略）
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    PLUGIN_MANUFACTURER_CODE  Bnca
    PLUGIN_CODE               Bbal
    BUNDLE_ID                 com.bounceaudio.bounceball
    FORMATS                   VST3 Standalone   # Standalone 用于脱离 DAW 排错
    PRODUCT_NAME              "BounceBall"
)

target_compile_definitions(BounceBall PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

if (MSVC)
    target_compile_options(BounceBall PRIVATE /utf-8)
endif()

target_sources(BounceBall PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(BounceBall PRIVATE Source)

target_link_libraries(BounceBall
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
