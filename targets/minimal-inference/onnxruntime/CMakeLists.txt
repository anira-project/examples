# SPDX-License-Identifier: Apache-2.0

add_executable(minimal-onnxruntime minimal-onnxruntime.cpp)
# The engine's API directly: anira::onnxruntime is the same engine library anira uses (one copy per
# process) and makes its headers reachable; anira::anira alone carries none.
target_link_libraries(minimal-onnxruntime PRIVATE anira_examples_minimal anira::onnxruntime)
if(TANH_OPERATING_SYSTEM STREQUAL "Windows" AND ANIRA_SHARED_LIBS_WIN)
    add_custom_command(TARGET minimal-onnxruntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ANIRA_SHARED_LIBS_WIN} $<TARGET_FILE_DIR:minimal-onnxruntime>)
endif()
