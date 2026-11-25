# SPDX-License-Identifier: Apache-2.0

add_executable(minimal-litert minimal-litert.cpp)
# The engine's API directly: anira::litert is the same engine library anira uses (one copy per
# process) and makes its headers reachable; anira::anira alone carries none.
target_link_libraries(minimal-litert PRIVATE anira_examples_minimal anira::litert)
if(TANH_OPERATING_SYSTEM STREQUAL "Windows" AND ANIRA_SHARED_LIBS_WIN)
    add_custom_command(TARGET minimal-litert POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ANIRA_SHARED_LIBS_WIN} $<TARGET_FILE_DIR:minimal-litert>)
endif()
