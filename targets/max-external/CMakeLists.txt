# SPDX-License-Identifier: Apache-2.0

# What every Max external shares: the Max SDK's headers (max-sdk-base) and how to link against the
# Max that loads it. Each example's <name>-max is External.cpp on its header (anira_example,
# cmake/examples.cmake), the object anira.<name>~.

set(_c74 "${max-sdk-base_SOURCE_DIR}/c74support")
add_library(anira_examples_max INTERFACE)
target_include_directories(anira_examples_max SYSTEM INTERFACE
    "${_c74}" "${_c74}/max-includes" "${_c74}/msp-includes")
if(TANH_OPERATING_SYSTEM STREQUAL "macOS")
    # Max provides the Max API when it loads the external; MSP comes from MaxAudioAPI.framework.
    target_link_options(anira_examples_max INTERFACE "LINKER:-undefined,dynamic_lookup")
    target_link_libraries(anira_examples_max INTERFACE "${_c74}/msp-includes/MaxAudioAPI.framework")
else()
    target_compile_definitions(anira_examples_max INTERFACE WIN_VERSION MAXAPI_USE_MSCRT _USE_MATH_DEFINES)
    target_link_libraries(anira_examples_max INTERFACE
        "${_c74}/max-includes/x64/MaxAPI.lib" "${_c74}/msp-includes/x64/MaxAudio.lib")
endif()
