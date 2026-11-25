# SPDX-License-Identifier: Apache-2.0

# The app's engine: the chosen example (ANIRA_MOBILE_EXAMPLE, from app.json) the way every target
# runs it (ExampleEngine), and the audio device's callback around it (ExampleHost). The AUv3 runs
# the same engine.
add_library(processor_host STATIC
    src/ExampleEngine.cpp
    src/ExampleHost.cpp
)
target_include_directories(processor_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(processor_host
    PUBLIC
    ${ANIRA_MOBILE_EXAMPLE}-example
    tanh::AudioIO
)
target_compile_features(processor_host PUBLIC cxx_std_20)
set_target_properties(processor_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
