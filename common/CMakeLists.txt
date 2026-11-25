# SPDX-License-Identifier: Apache-2.0

# What every target shares: the shape of an example (Example.h), the backend choice (Backends.h),
# the dry/wet mix (DryWet.h) and where the weights are found (Weights.h).

add_library(anira_examples_common STATIC src/Backends.cpp)
target_include_directories(anira_examples_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(anira_examples_common PUBLIC anira::models::core)
if(NOT TANH_OPERATING_SYSTEM STREQUAL "Windows")
    target_link_libraries(anira_examples_common PUBLIC ${CMAKE_DL_LIBS})
endif()
