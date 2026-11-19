cmake_minimum_required(VERSION 3.22)

project(TgsoundsEQ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(TG_BUILD_TESTS "Build the unit tests" ON)
option(TG_ENABLE_LTO "Enable link-time optimisation for release plugin builds" OFF)

# ----------------------------------------------------------------------------
# Dependencies (FetchContent). To build offline / faster, point FetchContent at
# local checkouts, e.g. -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

if(TG_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()

# ----------------------------------------------------------------------------
# Plugin
# ----------------------------------------------------------------------------
set(TG_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TG_FORMATS AU)
endif()

juce_add_plugin(TgsoundsEQ
    COMPANY_NAME                "Tgsounds"
    COMPANY_WEBSITE             "https://example.com/tgsounds"
    BUNDLE_ID                   "com.tgsounds.tgsoundseq"
    PLUGIN_MANUFACTURER_CODE    Tgsd
    PLUGIN_CODE                 TgEq
    FORMATS                     ${TG_FORMATS}
    PRODUCT_NAME                "TgsoundsEQ"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE      # MIDI Learn
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx EQ
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect)

file(GLOB_RECURSE TG_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.h)
target_sources(TgsoundsEQ PRIVATE ${TG_SOURCES})

target_include_directories(TgsoundsEQ PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/source)

target_compile_definitions(TgsoundsEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(TgsoundsEQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_opengl
        juce::juce_recommended_warning_flags
    PUBLIC
        juce::juce_recommended_config_flags)
# Exact float compares (structural zeros) and switch-with-default are intentional in the DSP code.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(SOURCE ${TG_SOURCES} APPEND PROPERTY COMPILE_OPTIONS -Wno-float-equal -Wno-switch-enum)
endif()
if(TG_ENABLE_LTO)
    target_link_libraries(TgsoundsEQ PRIVATE juce::juce_recommended_lto_flags)
endif()

# ----------------------------------------------------------------------------
# Tests: link against the plugin's shared-code static library.
# ----------------------------------------------------------------------------
if(TG_BUILD_TESTS)
    enable_testing()
    file(GLOB TG_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
    add_executable(TgsoundsEQTests ${TG_TEST_SOURCES})
    target_link_libraries(TgsoundsEQTests PRIVATE
        TgsoundsEQ
        juce::juce_audio_utils juce::juce_dsp juce::juce_opengl
        Catch2::Catch2)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(TgsoundsEQTests PRIVATE -Wno-float-equal)
    endif()
    include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)
    catch_discover_tests(TgsoundsEQTests)
endif()
