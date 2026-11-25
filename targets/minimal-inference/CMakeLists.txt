# SPDX-License-Identifier: Apache-2.0

# Each engine's own API on the effects' weights, without anira's handler (from anira's
# examples/minimal-inference): what anira does for you. One executable per engine of this build.

set(_weights_defines "")
foreach(_model guitar-lstm steerable-nafx stateful-lstm)
    anira_models_fetch(${PROJECT_SOURCE_DIR}/models/models/${_model}/${_model}.catalog.json
        OUT_DIR _dir)
    string(TOUPPER "${_model}" _upper)
    string(REPLACE "-" "_" _upper "${_upper}")
    list(APPEND _weights_defines ANIRA_MINIMAL_${_upper}_WEIGHTS="${_dir}")
endforeach()

add_library(anira_examples_minimal INTERFACE)
target_include_directories(anira_examples_minimal INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(anira_examples_minimal INTERFACE ${_weights_defines})
target_link_libraries(anira_examples_minimal INTERFACE
    anira::anira
    anira::models::guitar_lstm
    anira::models::steerable_nafx
    anira::models::stateful_lstm)

if(ANIRA_WITH_LIBTORCH)
    add_subdirectory(libtorch)
endif()
if(ANIRA_WITH_TFLITE)
    add_subdirectory(tensorflow-lite)
endif()
if(ANIRA_WITH_LITERT)
    add_subdirectory(litert)
endif()
if(ANIRA_WITH_EXECUTORCH)
    add_subdirectory(executorch)
endif()
if(ANIRA_WITH_ONNXRUNTIME)
    add_subdirectory(onnxruntime)
endif()
