# SPDX-License-Identifier: Apache-2.0

# What every standalone shares: CHOC (window, WebView, message loop), miniaudio (the audio device)
# and the page (ui.html, compiled in). Each example's <name>-standalone is Main.cpp on its header
# (anira_example, cmake/examples.cmake).

add_library(anira_examples_miniaudio STATIC Miniaudio.c)
target_include_directories(anira_examples_miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
if(TANH_OPERATING_SYSTEM STREQUAL "macOS")
    target_link_libraries(anira_examples_miniaudio PUBLIC
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(TANH_OPERATING_SYSTEM STREQUAL "Linux")
    find_package(Threads REQUIRED)
    target_link_libraries(anira_examples_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

anira_models_embed_text(anira_examples_standalone_ui
    NAMESPACE anira::examples::standalone
    HEADER anira/examples/standalone/Ui.h
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/ui.html)

add_library(anira_examples_standalone INTERFACE)
target_include_directories(anira_examples_standalone SYSTEM INTERFACE ${choc_SOURCE_DIR})
target_link_libraries(anira_examples_standalone INTERFACE
    anira_examples_miniaudio anira_examples_standalone_ui)
if(TANH_OPERATING_SYSTEM STREQUAL "macOS")
    target_link_libraries(anira_examples_standalone INTERFACE
        "-framework WebKit" "-framework AppKit")
elseif(TANH_OPERATING_SYSTEM STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ANIRA_EXAMPLES_WEBKIT REQUIRED IMPORTED_TARGET gtk+-3.0 webkit2gtk-4.1)
    target_link_libraries(anira_examples_standalone INTERFACE PkgConfig::ANIRA_EXAMPLES_WEBKIT)
endif()
