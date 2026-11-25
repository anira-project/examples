# SPDX-License-Identifier: Apache-2.0

# The app's C++: one example of the examples repository (ANIRA_MOBILE_EXAMPLE, from app.json's
# expo.extra.anira.example) on anira, with its model from anira-project/models (the repository's
# submodule), tanh-lib's AudioIO for the audio device, the engine and host around it and, on iOS, the
# AUv3. Built by gradle (native/jsi/CMakeLists.txt) on Android and by
# native/scripts/build-ios-libs.sh on iOS.

cmake_minimum_required(VERSION 3.28)

project(anira-mobile-example VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Everything is linked statically.
set(BUILD_SHARED_LIBS OFF)

set(ANIRA_EXAMPLES_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "anira-project/examples")
set(ANIRA_MODELS_ROOT "${ANIRA_EXAMPLES_ROOT}/models")

# The example the app runs: app.json's expo.extra.anira.example, one of effects/<name>.
file(READ "${CMAKE_CURRENT_LIST_DIR}/app.json" _app)
string(JSON _example ERROR_VARIABLE _no_example GET "${_app}" expo extra anira example)
if(_no_example)
    set(_example silero-vad)
endif()
set(ANIRA_MOBILE_EXAMPLE "${_example}" CACHE STRING "The example the app runs (effects/<name>)")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/app.json")
message(STATUS "anira mobile example: ${ANIRA_MOBILE_EXAMPLE}")

# anira's engines: ONNX Runtime and LiteRT ship static libraries for Android and iOS
# (anira-project/backends); the app switches between them at run time.
option(ANIRA_WITH_ONNXRUNTIME "Build anira with ONNX Runtime" ON)
option(ANIRA_WITH_LITERT "Build anira with LiteRT" ON)
set(ANIRA_WITH_TFLITE OFF CACHE BOOL "" FORCE)
set(ANIRA_WITH_LIBTORCH OFF CACHE BOOL "LibTorch has no Android/iOS build" FORCE)
set(ANIRA_WITH_EXECUTORCH OFF CACHE BOOL "The app ships no ExecuTorch program" FORCE)

# tanh-lib with AudioIO (the microphone) besides what models needs (Core, DSP), at the commit models
# pins: declared here first, models then uses it.
set(TANH_BUILD_CORE ON)
set(TANH_BUILD_DSP ON)
set(TANH_BUILD_AUDIO_IO ON)
set(TANH_BUILD_STATE OFF)
set(TANH_BUILD_MODULATION OFF)
set(TANH_BUILD_MIDI OFF)
set(TANH_BUILD_NET OFF)
set(TANH_WITH_TESTS OFF)
set(TANH_WITH_EXAMPLES OFF)
set(TANH_WITH_DOCS OFF)
set(TANH_WITH_INSTALL OFF)
include(FetchContent)
FetchContent_Declare(tanh-lib
    GIT_REPOSITORY https://github.com/tanh-lab/tanh-lib.git
    GIT_TAG e999959cf80b3e86f0701a325803438fe79060bd)
FetchContent_MakeAvailable(tanh-lib)

# Every model library an example may use (all but silero_vad are header-only); only the chosen
# example fetches weights.
foreach(_model GUITAR_LSTM STATEFUL_LSTM STEERABLE_NAFX SILERO_VAD
               RAVE_DJEMBE RAVE_FUNK_DRUM RAVE_MAGNETIC_RESONATOR_PIANO)
    set(ANIRA_MODELS_WITH_${_model} ON)
endforeach()
add_subdirectory(${ANIRA_MODELS_ROOT} models)

# The examples repository's example machinery, for the one example, its weights compiled in.
set(ANIRA_EXAMPLES_WITH_EFFECTS ON)
set(ANIRA_EXAMPLES_WITH_RAVE ON)
set(ANIRA_EXAMPLES_WITH_JUCE OFF)
set(ANIRA_EXAMPLES_WITH_CLAP OFF)
set(ANIRA_EXAMPLES_WITH_STANDALONE OFF)
set(ANIRA_EXAMPLES_ONLY ${ANIRA_MOBILE_EXAMPLE})
set(ANIRA_EXAMPLES_EMBED_ALL ON)
include(${ANIRA_EXAMPLES_ROOT}/cmake/examples.cmake)
add_subdirectory(${ANIRA_EXAMPLES_ROOT}/common common)
add_subdirectory(${ANIRA_EXAMPLES_ROOT}/effects effects)
if(NOT TARGET ${ANIRA_MOBILE_EXAMPLE}-example)
    message(FATAL_ERROR "ANIRA_MOBILE_EXAMPLE: no example '${ANIRA_MOBILE_EXAMPLE}' in effects/")
endif()

add_subdirectory(native/host)

# The AUv3 plugin wraps the same example for iOS plugin hosts.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_subdirectory(native/auv3)

    # What native/scripts/build-ios-libs.sh stages for Xcode (the pod and the AUv3 extension
    # compile against it): anira's source (the engines' xcframeworks are there), every include
    # directory and compile definition the native code sees, '|'-separated.
    FetchContent_GetProperties(anira)
    set(_includes "$<TARGET_PROPERTY:processor_host,INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:anira_auv3,INCLUDE_DIRECTORIES>")
    set(_defines "$<TARGET_PROPERTY:processor_host,COMPILE_DEFINITIONS>")
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/staging.env" CONTENT
"ANIRA_SOURCE_DIR='${anira_SOURCE_DIR}'
INCLUDE_DIRS='$<JOIN:$<REMOVE_DUPLICATES:${_includes}>,|>'
DEFINES='$<JOIN:$<REMOVE_DUPLICATES:${_defines}>,|>'
")
endif()
