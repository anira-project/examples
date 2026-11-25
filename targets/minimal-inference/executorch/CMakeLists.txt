# SPDX-License-Identifier: Apache-2.0

add_executable(minimal-executorch minimal-executorch.cpp)
# The ExecuTorch runtime directly, without anira: anira::executorch carries the runtime, kernel and
# XNNPACK-delegate archives with their link options and the headers' definitions.
target_link_libraries(minimal-executorch PRIVATE anira::executorch)
target_compile_definitions(minimal-executorch PRIVATE
    $<TARGET_PROPERTY:anira_examples_minimal,INTERFACE_COMPILE_DEFINITIONS>)
