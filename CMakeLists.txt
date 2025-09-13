cmake_minimum_required(VERSION 3.15)

project(SamuRiser VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_SOURCE_DIR}/deps/juce" CACHE PATH "Path to JUCE framework")
endif()

if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.0
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(SamuRiser
    COMPANY_NAME "Driftkind"
    PLUGIN_MANUFACTURER_CODE Drft
    PLUGIN_CODE SmRs
    FORMATS VST3 AU
    IS_SYNTH TRUE
    PRODUCT_NAME "SamuRiser"
)

target_sources(SamuRiser PRIVATE
    src/SamuRiserAudioProcessor.cpp
    src/SamuRiserAudioProcessor.h
    src/GranularEngine.cpp
    src/GranularEngine.h
    src/SpectralEngine.cpp
    src/SpectralEngine.h
    src/HybridEngine.cpp
    src/HybridEngine.h
    src/SamuRiserAudioEditor.cpp
    src/SamuRiserAudioEditor.h
    src/SammuMatcher.cpp
    src/SammuMatcher.h
)

target_compile_definitions(SamuRiser PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

if(MSVC)
    target_compile_options(SamuRiser PRIVATE /W4 /permissive-)
else()
    target_compile_options(SamuRiser PRIVATE -Wall -Wextra -Wpedantic)
endif()

juce_generate_juce_header(SamuRiser)

target_link_libraries(SamuRiser PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_dsp
)

enable_testing()
add_executable(SamuRiserTests
    tests/test_main.cpp
    src/SamuRiserAudioProcessor.cpp
    src/GranularEngine.cpp
    src/SpectralEngine.cpp
    src/HybridEngine.cpp
    src/SammuMatcher.cpp
)
target_include_directories(SamuRiserTests PRIVATE src)
target_link_libraries(SamuRiserTests PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_dsp
)
add_test(NAME SamuRiserTests COMMAND SamuRiserTests)

set(CPACK_PACKAGE_NAME "SamuRiser")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")
include(CPack)

option(SAMURISE_USE_RUBBERBAND "Use Rubber Band time-stretching" OFF)
if(SAMURISE_USE_RUBBERBAND)
    find_package(RubberBand REQUIRED)
    target_link_libraries(SamuRiser PRIVATE RubberBand::rubberband)
    target_link_libraries(SamuRiserTests PRIVATE RubberBand::rubberband)
    target_compile_definitions(SamuRiser PRIVATE SAMURISE_USE_RUBBERBAND=1)
    target_compile_definitions(SamuRiserTests PRIVATE SAMURISE_USE_RUBBERBAND=1)
endif()

if(APPLE AND DEFINED CODESIGN_IDENTITY)
    add_custom_command(TARGET SamuRiser POST_BUILD
        COMMAND codesign --force --sign "${CODESIGN_IDENTITY}" $<TARGET_FILE:SamuRiser>)
endif()

if(WIN32 AND DEFINED SIGNTOOL_EXE)
    add_custom_command(TARGET SamuRiser POST_BUILD
        COMMAND "${SIGNTOOL_EXE}" sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 $<TARGET_FILE:SamuRiser>)
endif()

