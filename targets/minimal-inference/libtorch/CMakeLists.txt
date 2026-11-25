# SPDX-License-Identifier: Apache-2.0

add_executable(minimal-libtorch minimal-libtorch.cpp)
# The engine's API directly: anira::libtorch is the same engine library anira uses (one copy per
# process) and makes its headers reachable; anira::anira alone carries none.
target_link_libraries(minimal-libtorch PRIVATE anira_examples_minimal anira::libtorch)
if(TANH_OPERATING_SYSTEM STREQUAL "Windows" AND ANIRA_SHARED_LIBS_WIN)
    add_custom_command(TARGET minimal-libtorch POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ANIRA_SHARED_LIBS_WIN} $<TARGET_FILE_DIR:minimal-libtorch>)
endif()
