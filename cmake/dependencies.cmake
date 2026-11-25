# SPDX-License-Identifier: Apache-2.0

# The targets' frameworks, at pinned versions. anira, tanh-lib and nlohmann_json come with models.

include(FetchContent)

if(ANIRA_EXAMPLES_WITH_JUCE)
    # JUCE 8 is AGPL-3.0 or commercial: a plugin built with it is under JUCE's licence.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.14
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(ANIRA_EXAMPLES_WITH_STANDALONE)
    # CHOC (header-only: the window, the WebView, the message loop) and miniaudio (the audio device).
    FetchContent_Declare(choc
        GIT_REPOSITORY https://github.com/Tracktion/choc.git
        GIT_TAG 9606a6615b592605993d92733cdfd6ca6ecbd825)
    FetchContent_Declare(miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.24
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(choc)
    if(NOT choc_POPULATED)
        FetchContent_Populate(choc)
    endif()
    FetchContent_GetProperties(miniaudio)
    if(NOT miniaudio_POPULATED)
        FetchContent_Populate(miniaudio)
    endif()
endif()

if(ANIRA_EXAMPLES_WITH_CLAP)
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG 1.2.10
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_Declare(clap-helpers
        GIT_REPOSITORY https://github.com/free-audio/clap-helpers.git
        GIT_TAG a380c978f429103e08afc1d17e9efbf3e3c96ee2
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(clap clap-helpers)

    # clap-wrapper: make_clapfirst_plugins() wraps one CLAP into VST3 and AUv2; it takes the clap
    # above and downloads the VST3 and AudioUnit SDKs itself.
    set(CLAP_SDK_ROOT "${clap_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
    set(CLAP_WRAPPER_DONT_ADD_TARGETS ON CACHE BOOL "" FORCE)
    FetchContent_Declare(clap-wrapper
        GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
        GIT_TAG v0.18.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(clap-wrapper)
endif()
