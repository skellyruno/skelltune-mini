cmake_minimum_required(VERSION 3.22)
project(SkellyTune VERSION 3.3.1 LANGUAGES C CXX)

# Based on skell-vst-template's CMake/JUCE 8 approach. The original template's
# demo gain processor has been replaced by the SkellyTune v3.3 DSP and GUI.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SKELLY_BUILD_PLUGIN "Build SkellyTune JUCE VST3" ON)
option(SKELLY_BUILD_DSP_TESTS "Build portable DSP tests and analysis tools" OFF)
option(SKELLY_BUILD_STUDIO "Include the separate 50 ms Studio plugin" OFF)
option(SKELLY_BUILD_STUDIO_FAST "Include the separate 40 ms Studio Fast plugin" OFF)

add_library(skelly_dsp INTERFACE)
target_include_directories(skelly_dsp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/dsp")
target_compile_features(skelly_dsp INTERFACE cxx_std_17)
if(MSVC)
    target_compile_options(skelly_dsp INTERFACE /W4)
else()
    target_compile_options(skelly_dsp INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(SKELLY_BUILD_DSP_TESTS)
    include(CTest)
    include(cmake/SkellyDSPTests.cmake)
endif()

if(SKELLY_BUILD_PLUGIN)
    include(FetchContent)
    # Allow a preinstalled JUCE tree for offline/local builds.
    if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("${JUCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
    else()
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.12
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()


    # SkellCore is pinned at an immutable revision. This provides UI theme
    # primitives and optional local WorldLink, not the imported tuning DSP.
    FetchContent_Declare(SkellCoreShared
        GIT_REPOSITORY https://github.com/skellyruno/skell-vst-template.git
        GIT_TAG d3bbdd0248bd9e33d2b9f3aa1a8caf7443d02172
        SOURCE_SUBDIR SkellCore)
    FetchContent_MakeAvailable(SkellCoreShared)

    function(skelly_add_plugin target bundle_id plugin_code product_name)
        juce_add_plugin(${target}
            COMPANY_NAME "Skelly Audio"
            BUNDLE_ID "${bundle_id}"
            PLUGIN_MANUFACTURER_CODE Skly
            PLUGIN_CODE ${plugin_code}
            IS_SYNTH FALSE
            NEEDS_MIDI_INPUT FALSE
            NEEDS_MIDI_OUTPUT FALSE
            IS_MIDI_EFFECT FALSE
            EDITOR_WANTS_KEYBOARD_FOCUS FALSE
            COPY_PLUGIN_AFTER_BUILD FALSE
            FORMATS VST3
            PRODUCT_NAME "${product_name}")
        target_sources(${target} PRIVATE
            Source/PluginProcessor.cpp Source/PluginProcessor.h
            Source/PluginEditor.cpp Source/PluginEditor.h)
        target_include_directories(${target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/dsp")
        target_link_libraries(${target} PRIVATE
            skelly_dsp
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
            SkellCore::DSP SkellCore::UI)
        target_compile_definitions(${target} PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    endfunction()


    # CI-only startup check: instantiate the same real processor and JUCE editor
    # used by the plugin. This is built and run on Windows but NOT distributed.
    option(SKELLY_BUILD_GUI_SMOKE "Build realtime plugin/editor startup smoke test" OFF)
    if(SKELLY_BUILD_GUI_SMOKE)
        juce_add_console_app(SkellyTuneGuiSmoke PRODUCT_NAME "SkellyTune GUI Smoke")
        target_sources(SkellyTuneGuiSmoke PRIVATE
            tests/gui_startup_smoke.cpp
            Source/PluginProcessor.cpp Source/PluginProcessor.h
            Source/PluginEditor.cpp Source/PluginEditor.h)
        target_include_directories(SkellyTuneGuiSmoke PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/dsp")
        target_link_libraries(SkellyTuneGuiSmoke PRIVATE
            skelly_dsp juce::juce_audio_utils juce::juce_audio_processors
            juce::juce_dsp juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
            SkellCore::DSP SkellCore::UI)
        target_compile_definitions(SkellyTuneGuiSmoke PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    endif()

    # Keep the v3.3 VST3 IDs unchanged so projects and presets can continue
    # recognizing the same plugin rather than the template gain plugin.
    skelly_add_plugin(SkellyTuneRealtime
        "com.skelly.realtimetune" SkRT "SkellyTune Realtime")

    if(SKELLY_BUILD_STUDIO)
        skelly_add_plugin(SkellyTuneStudio
            "com.skelly.studiotune" SkST "SkellyTune Studio")
        target_compile_definitions(SkellyTuneStudio PRIVATE SKELLY_STUDIO_PROFILE=1)
    endif()

    if(SKELLY_BUILD_STUDIO_FAST)
        skelly_add_plugin(SkellyTuneStudioFast
            "com.skelly.studiotunefast" SkSF "SkellyTune Studio Fast")
        target_compile_definitions(SkellyTuneStudioFast PRIVATE SKELLY_STUDIO_FAST_PROFILE=1)
    endif()
endif()
