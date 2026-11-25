# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

# Version from the nearest vX.Y.Z tag (0.0.0 without one) — before project().
include(cmake/tanh/git-version.cmake)
tanh_git_version(${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "The oldest macOS the examples run on")

project(anira-examples VERSION ${TANH_VERSION_SHORT} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Each static library once on a link line (CMake 4.1+; Apple's ld warns about repeats).
set(CMAKE_LINK_LIBRARIES_STRATEGY REORDER_FREELY)

# Platform axes: TANH_OPERATING_SYSTEM, TANH_BINARY_FORMAT. Every OS decision keys on these.
include(cmake/tanh/platform.cmake)
if(TANH_OPERATING_SYSTEM STREQUAL "macOS")
    # clap-wrapper's AUv2 and standalone wrappers are Objective-C++.
    enable_language(OBJC OBJCXX)
endif()

# ------------------------------------------------------------------------------
# Options. The inference engines are anira's (ANIRA_WITH_*).
# ------------------------------------------------------------------------------
option(ANIRA_EXAMPLES_WITH_EFFECTS
       "The small audio effects: steerable-nafx, GuitarLSTM, the stateful LSTM" ON)
option(ANIRA_EXAMPLES_WITH_RAVE
       "The RAVE examples (weights CC BY-NC 4.0, 50 to 130 MB per graph and engine)" OFF)
option(ANIRA_EXAMPLES_WITH_JUCE "Every example as a JUCE plugin (VST3, AU, Standalone)" ON)
option(ANIRA_EXAMPLES_WITH_CLAP "Every example as a CLAP plugin (CLAP, VST3, AUv2 through clap-wrapper)" ON)
option(ANIRA_EXAMPLES_WITH_MINIMAL_INFERENCE "Each engine's own API on the effects' weights, without anira" ON)
option(ANIRA_EXAMPLES_LINK_COMPILE_COMMANDS
       "Link build/compile_commands.json, which clangd reads, to this tree's" OFF)

if(ANIRA_EXAMPLES_LINK_COMPILE_COMMANDS)
    include(cmake/tanh/compile-commands.cmake)
    tanh_link_compile_commands()
endif()

# ------------------------------------------------------------------------------
# models (the submodule): anira, tanh-lib and the model libraries the examples use.
# ------------------------------------------------------------------------------
foreach(_model GUITAR_LSTM STATEFUL_LSTM STEERABLE_NAFX)
    set(ANIRA_MODELS_WITH_${_model} ${ANIRA_EXAMPLES_WITH_EFFECTS})
endforeach()
foreach(_model RAVE_DJEMBE RAVE_FUNK_DRUM RAVE_MAGNETIC_RESONATOR_PIANO)
    set(ANIRA_MODELS_WITH_${_model} ${ANIRA_EXAMPLES_WITH_RAVE})
endforeach()
add_subdirectory(models)

message(STATUS "anira-examples ${TANH_VERSION_FULL} on ${TANH_OPERATING_SYSTEM} (${TANH_BINARY_FORMAT})")

# JUCE, clap, clap-helpers, clap-wrapper (cmake/dependencies.cmake).
include(cmake/dependencies.cmake)
# anira_example(): an example header on every target (cmake/examples.cmake).
include(cmake/examples.cmake)

add_subdirectory(common)
add_subdirectory(effects)
if(ANIRA_EXAMPLES_WITH_MINIMAL_INFERENCE AND ANIRA_EXAMPLES_WITH_EFFECTS)
    add_subdirectory(targets/minimal-inference)
endif()
