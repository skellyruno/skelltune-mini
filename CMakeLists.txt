cmake_minimum_required(VERSION 3.22)
project(SkellyTune VERSION 3.3.2 LANGUAGES C CXX)

# Based on skellcore-vst-template's CMake/JUCE 8 approach. The original template's
# demo gain processor has been replaced by the SkellyTune v3.3 DSP and GUI.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SKELLY_BUILD_PLUGIN "Build SkellyTune JUCE VST3" ON)
option(SKELLY_BUILD_DSP_TESTS "Build portable DSP tests and analysis tools" OFF)
option(SKELLY_BUILD_STUDIO "Include the separate 50 ms Studio plugin" OFF)
option(SKELLY_BUILD_STUDIO_FAST "Include the separate 40 ms Studio Fast plugin" OFF)
# TEST ONLY. Experimental META implementation must never be enabled in distributable builds.
option(SKELLY_META_TEST "TEST ONLY: include the experimental META engine. Never ship." OFF)
if(SKELLY_META_TEST)
    message(WARNING "SKELLY_META_TEST is ON: this build contains test-only META code. Do not release it.")
    add_compile_definitions(SKELLY_META_TEST=1)
endif()

# Independent research product; all dependency source roots must be explicit.
option(SKELLY_BUILD_PITCH_PLAYGROUND "Build source-backed pitch comparison laboratory" OFF)
option(SKELLY_PLAYGROUND_FULL "Enable OG and MetaTune Inspired source adapters (licence review required before distribution)" OFF)
set(SKELLY_SNAP_HEADER "" CACHE FILEPATH "Recovered, verified SnapCore.h from pinned source")
set(SKELLY_OG_SOURCE "" CACHE PATH "Pinned OG SkellyTune source checkout")
set(SKELLY_META_SOURCE "" CACHE PATH "Pinned MetaTune Inspired source checkout")
if(SKELLY_BUILD_PITCH_PLAYGROUND)
    if(NOT SKELLY_META_TEST OR SKELLY_BUILD_STUDIO OR SKELLY_BUILD_STUDIO_FAST)
        message(FATAL_ERROR "Playground is research-only; require SKELLY_META_TEST and disable Studio targets")
    endif()
    if(NOT EXISTS "${SKELLY_SNAP_HEADER}")
        message(FATAL_ERROR "Provide the recovered pinned SnapCore.h, not a substitute implementation")
    endif()
    file(SHA256 "${SKELLY_SNAP_HEADER}" snap_hash)
    if(NOT snap_hash STREQUAL "ca46212c500efe397968a2ef9869050f26b0ea015b5f462fd8ac99ffb6b3672b")
        message(FATAL_ERROR "SnapCore.h does not match the pinned verified Classic/Crystal source")
    endif()
    get_filename_component(SKELLY_SNAP_INCLUDE "${SKELLY_SNAP_HEADER}" DIRECTORY)
endif()
if(SKELLY_PLAYGROUND_FULL AND (NOT SKELLY_BUILD_PITCH_PLAYGROUND OR NOT SKELLY_BUILD_PLUGIN))
    message(FATAL_ERROR "Full playground requires the JUCE research build")
endif()

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
    if(SKELLY_BUILD_PITCH_PLAYGROUND)
        # Portable 5-engine reference regardless of whether JUCE is available.
        add_executable(playground_bank_test tests/playground_bank_test.cpp)
        target_link_libraries(playground_bank_test PRIVATE skelly_dsp)
        target_include_directories(playground_bank_test PRIVATE "${SKELLY_SNAP_INCLUDE}")
        add_test(NAME playground_bank_test COMMAND playground_bank_test)
        add_executable(playground_mix_test tests/playground_mix_test.cpp)
        target_link_libraries(playground_mix_test PRIVATE skelly_dsp)
        target_include_directories(playground_mix_test PRIVATE "${SKELLY_SNAP_INCLUDE}")
        add_test(NAME playground_mix_test COMMAND playground_mix_test)
        add_executable(playground_attack_test tests/playground_attack_test.cpp)
        target_link_libraries(playground_attack_test PRIVATE skelly_dsp)
        target_include_directories(playground_attack_test PRIVATE "${SKELLY_SNAP_INCLUDE}")
        add_test(NAME playground_attack_test COMMAND playground_attack_test)
        add_executable(playground_recovery_test tests/playground_recovery_test.cpp)
        target_link_libraries(playground_recovery_test PRIVATE skelly_dsp)
        target_include_directories(playground_recovery_test PRIVATE "${SKELLY_SNAP_INCLUDE}")
        add_test(NAME playground_recovery_test COMMAND playground_recovery_test)
        add_executable(playground_render tools/render.cpp)
        target_link_libraries(playground_render PRIVATE skelly_dsp)
        target_include_directories(playground_render PRIVATE "${SKELLY_SNAP_INCLUDE}")
        target_compile_definitions(playground_render PRIVATE SKELLY_PLAYGROUND_RENDER=1)
    endif()
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
        GIT_REPOSITORY https://github.com/skellyruno/skellcore-vst-template.git
        GIT_TAG 4c499c58cb3b2791eed2d5dcef6d8d543fcaea8d
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

    # LOCAL-ONLY real JUCE editor screenshots. Not a VST3/Standalone product,
    # not an audio-waveform renderer, and never shipped to customers.
    # Can build offline with -DJUCE_PATH=... and
    # -DFETCHCONTENT_SOURCE_DIR_SKELLCORESHARED=... (pinned source).
    option(SKELLY_BUILD_UI_QUICK4 "Build local native SkellyTune Mini UI renderer" OFF)
    if(SKELLY_BUILD_UI_QUICK4)
        juce_add_console_app(SkellyTuneQuick4 PRODUCT_NAME "SkellyTune Mini Quick4 UI Renderer")
        target_sources(SkellyTuneQuick4 PRIVATE
            tests/ui_quick4.cpp
            Source/PluginProcessor.cpp Source/PluginProcessor.h
            Source/PluginEditor.cpp Source/PluginEditor.h)
        target_include_directories(SkellyTuneQuick4 PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/dsp")
        target_link_libraries(SkellyTuneQuick4 PRIVATE
            skelly_dsp juce::juce_audio_utils juce::juce_audio_processors
            juce::juce_dsp juce::juce_recommended_warning_flags
            SkellCore::DSP SkellCore::UI)
        # UI renderer only: no release-O3. Avoid unnecessary memory usage
        # when compiling JUCE locally; the shipped VST3 keeps release flags.
        target_compile_options(SkellyTuneQuick4 PRIVATE
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O0>)
        target_compile_definitions(SkellyTuneQuick4 PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    endif()

    # Keep the v3.3 VST3 IDs unchanged so projects and presets can continue
    # recognizing the same plugin rather than the template gain plugin.
    # A separately identified, TEST-ONLY companion. Never overwrite a user's
    # installed SkellyTune Realtime VST3 with an experimental META binary.
    # Normal builds retain the existing manufacturer/product codes and automation.
    if(SKELLY_BUILD_PITCH_PLAYGROUND)
        if(SKELLY_PLAYGROUND_FULL)
            if(NOT EXISTS "${SKELLY_OG_SOURCE}/Source/AutoTuneEngine.h" OR
               NOT EXISTS "${SKELLY_META_SOURCE}/Source/SkellyFusionEngine.h")
                message(FATAL_ERROR "Provide both pinned source checkouts for the seven-engine playground")
            endif()
            skelly_add_plugin(SkellyTunePlayground "com.skelly.pitchplayground" SkP7 "SkellyTune Playground")
        else()
            skelly_add_plugin(SkellyTunePlayground "com.skelly.pitchplayground.five" SkP5 "SkellyTune Playground 5")
        endif()
        function(configure_playground_sources target)
            target_include_directories(${target} PRIVATE "${SKELLY_SNAP_INCLUDE}" "${CMAKE_CURRENT_SOURCE_DIR}/dsp")
            target_compile_definitions(${target} PRIVATE SKELLY_PITCH_PLAYGROUND=1)
            if(SKELLY_PLAYGROUND_FULL)
                # OG uses explicit template-parameter lambdas in its SIMD
                # shifter. Its original build requires C++20; do not rewrite
                # or disable the SIMD engine merely to make C++17 compile.
                target_compile_features(${target} PRIVATE cxx_std_20)
                juce_generate_juce_header(${target})
                target_include_directories(${target} PRIVATE "${SKELLY_OG_SOURCE}/Source" "${SKELLY_META_SOURCE}/Source")
                target_compile_definitions(${target} PRIVATE SKELLY_PLAYGROUND_FULL=1
                    SKELLTUNE_AUTO_2P5=1 SKELLTUNE_LIVE_FIXED_LOW_LATENCY=1
                    SKELLTUNE_LOW_VOICE_LAB=1 SKELLTUNE_LAB_FAST_2P5=1)
                foreach(src IN ITEMS TunerChain.cpp Pitch/YinDetector.cpp Pitch/TargetSelector.cpp
                        Pitch/CorrectionEngine.cpp Pitch/PitchShifter.cpp Doubler.cpp FormantAccent.cpp)
                    target_sources(${target} PRIVATE "${SKELLY_META_SOURCE}/Source/LyraDSP/${src}")
                endforeach()
            endif()
        endfunction()
        configure_playground_sources(SkellyTunePlayground)
        # The same processor/editor as the VST3, plus independent direct-bank tests.
        juce_add_console_app(SkellyPlaygroundSmoke PRODUCT_NAME "SkellyTune Playground QA")
        target_sources(SkellyPlaygroundSmoke PRIVATE tests/playground_gui_smoke.cpp
            Source/PluginProcessor.cpp Source/PluginEditor.cpp)
        target_link_libraries(SkellyPlaygroundSmoke PRIVATE skelly_dsp juce::juce_audio_utils
            juce::juce_audio_processors juce::juce_dsp SkellCore::DSP SkellCore::UI)
        target_compile_definitions(SkellyPlaygroundSmoke PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
        configure_playground_sources(SkellyPlaygroundSmoke)
        if(SKELLY_PLAYGROUND_FULL)
            juce_add_console_app(SkellyPlaygroundFullBankTest PRODUCT_NAME "Playground seven-engine DSP QA")
            target_sources(SkellyPlaygroundFullBankTest PRIVATE tests/playground_bank_test.cpp)
            target_link_libraries(SkellyPlaygroundFullBankTest PRIVATE skelly_dsp juce::juce_audio_utils juce::juce_dsp)
            target_compile_definitions(SkellyPlaygroundFullBankTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
            configure_playground_sources(SkellyPlaygroundFullBankTest)
            juce_add_console_app(SkellyPlaygroundFullMixTest PRODUCT_NAME "Playground seven-engine mix QA")
            target_sources(SkellyPlaygroundFullMixTest PRIVATE tests/playground_mix_test.cpp)
            target_link_libraries(SkellyPlaygroundFullMixTest PRIVATE skelly_dsp juce::juce_audio_utils juce::juce_dsp)
            target_compile_definitions(SkellyPlaygroundFullMixTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
            configure_playground_sources(SkellyPlaygroundFullMixTest)
            juce_add_console_app(SkellyPlaygroundFullAttackTest PRODUCT_NAME "Playground seven-engine attack QA")
            target_sources(SkellyPlaygroundFullAttackTest PRIVATE tests/playground_attack_test.cpp)
            target_link_libraries(SkellyPlaygroundFullAttackTest PRIVATE skelly_dsp juce::juce_audio_utils juce::juce_dsp)
            target_compile_definitions(SkellyPlaygroundFullAttackTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
            configure_playground_sources(SkellyPlaygroundFullAttackTest)
            juce_add_console_app(SkellyPlaygroundFullRecoveryTest PRODUCT_NAME "Playground seven-engine recovery QA")
            target_sources(SkellyPlaygroundFullRecoveryTest PRIVATE tests/playground_recovery_test.cpp)
            target_link_libraries(SkellyPlaygroundFullRecoveryTest PRIVATE skelly_dsp juce::juce_audio_utils juce::juce_dsp)
            target_compile_definitions(SkellyPlaygroundFullRecoveryTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
            configure_playground_sources(SkellyPlaygroundFullRecoveryTest)
            juce_add_console_app(SkellyPlaygroundFullRender PRODUCT_NAME "Playground seven-engine WAV renderer")
            target_sources(SkellyPlaygroundFullRender PRIVATE tools/render.cpp)
            target_link_libraries(SkellyPlaygroundFullRender PRIVATE skelly_dsp juce::juce_audio_utils juce::juce_dsp)
            target_compile_definitions(SkellyPlaygroundFullRender PRIVATE SKELLY_PLAYGROUND_RENDER=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
            configure_playground_sources(SkellyPlaygroundFullRender)
        endif()
    elseif(SKELLY_META_TEST)
        skelly_add_plugin(SkellyTuneMetaLab
            "com.skelly.realtimetune.metalab" SkML "SkellyTune META Lab")
    else()
        skelly_add_plugin(SkellyTuneRealtime
            "com.skelly.realtimetune" SkRT "SkellyTune Realtime")
    endif()

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
