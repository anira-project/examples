cmake_minimum_required(VERSION 3.28)

# React Native auto-loads libappmodules.so — the CMake project() must be named
# "appmodules" or the native module won't register.
project(appmodules)

# Pulls in the React Native application CMake setup (codegen, fbjni, ...)
include(${REACT_ANDROID_DIR}/cmake-utils/ReactNative-application.cmake)

set(EXAMPLE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

# Build the whole example C++ stack (anira + audio-io + backend) into this build
add_subdirectory(${EXAMPLE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/anira_example_build)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${EXAMPLE_ROOT}/native/jsi/src/NativeAniraJSI.cpp
    # OnLoad.cpp has to be next to this CMakeLists.txt so that React Native's
    # CMake understands to not include its default OnLoad.cpp
    ${EXAMPLE_ROOT}/native/jsi/OnLoad.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
    ${EXAMPLE_ROOT}/native/jsi/include
)

target_link_libraries(${CMAKE_PROJECT_NAME} processor_host)

if(ANDROID)
    target_link_libraries(${CMAKE_PROJECT_NAME} log)
endif()
