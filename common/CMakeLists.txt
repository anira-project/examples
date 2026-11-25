# SPDX-License-Identifier: Apache-2.0

# What every target shares: the shape of an example (Example.h), the backend choice (Backends.h),
# the dry/wet mix (DryWet.h) and where the weights are found (Weights.h).

add_library(anira_examples_common STATIC src/Backends.cpp)
target_include_directories(anira_examples_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(anira_examples_common PUBLIC anira::models::core)
if(NOT TANH_OPERATING_SYSTEM STREQUAL "Windows")
    target_link_libraries(anira_examples_common PUBLIC ${CMAKE_DL_LIBS})
endif()

# The camera, for the camera examples (anira_example(... CAMERA)): AVFoundation on Apple
# platforms, OpenCV elsewhere or with ANIRA_EXAMPLES_CAMERA=opencv.
if(ANIRA_EXAMPLES_WITH_HANDS)
    if(TANH_OPERATING_SYSTEM MATCHES "^(macOS|iOS)$" AND NOT ANIRA_EXAMPLES_CAMERA STREQUAL "opencv")
        add_library(anira_examples_camera STATIC camera/src/CameraAvFoundation.mm)
        set_source_files_properties(camera/src/CameraAvFoundation.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
        target_link_libraries(anira_examples_camera PUBLIC
            "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo")
    else()
        find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)
        add_library(anira_examples_camera STATIC camera/src/CameraOpenCv.cpp)
        target_link_libraries(anira_examples_camera PRIVATE ${OpenCV_LIBS})
        target_include_directories(anira_examples_camera SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
    endif()
    target_include_directories(anira_examples_camera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/camera/include)
    target_compile_features(anira_examples_camera PUBLIC cxx_std_20)
endif()
