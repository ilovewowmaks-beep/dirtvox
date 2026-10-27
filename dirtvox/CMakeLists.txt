cmake_minimum_required(VERSION 3.22)
project(DirtVox VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Статический рантайм MSVC: плагину не нужен Visual C++ Redistributable на компьютере с FL.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(DIRTVOX_TESTS "Собрать консольный тест-рендер" OFF)

# JUCE: локальная папка через -DJUCE_PATH=..., иначе скачивается автоматически.
if(DEFINED JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(DirtVox
    COMPANY_NAME "AngelStudio"
    PLUGIN_MANUFACTURER_CODE Angl
    PLUGIN_CODE Drtv
    FORMATS VST3
    PRODUCT_NAME "Dirt Vox"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DirtVox PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(DirtVox PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(DirtVox
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(DirtVox PRIVATE /utf-8)
endif()

if(DIRTVOX_TESTS)
    juce_add_console_app(DirtVoxTest PRODUCT_NAME "DirtVoxTest")
    target_sources(DirtVoxTest PRIVATE tests/RenderTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(DirtVoxTest PRIVATE Source)
    target_compile_definitions(DirtVoxTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(DirtVoxTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats juce::juce_recommended_config_flags)
endif()
