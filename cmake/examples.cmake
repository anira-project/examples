# SPDX-License-Identifier: Apache-2.0

# An example: a header that maps a model wrapper onto the targets.
#
#   anira_example(<name>
#       HEADER <Example.h>       defines anira::examples::Example (common/include/anira/examples/Example.h)
#       LINK <target>...         its model libraries (anira::models::<model>)
#       CATALOG <catalog.json>   its weights on Hugging Face (anira_models_fetch)
#       PRODUCT <name>           the plugin's name
#       CODE <code>              four characters, the first upper-case, the rest lower-case
#       [EMBED])                 the weights compiled into the binary; else copied into the bundle,
#                                <binary dir>/../Resources/models/<repo>
#
# Gives <name>-example (an INTERFACE target: the header, the model libraries, the weights) and, per
# enabled target, <name>-juce (VST3, AU, Standalone), <name>-clap (CLAP, VST3, AUv2) and
# <name>-standalone (CHOC + miniaudio).

include_guard(GLOBAL)

set(_anira_examples_dir "${CMAKE_CURRENT_LIST_DIR}/..")

function(anira_example name)
    cmake_parse_arguments(PARSE_ARGV 1 _arg "EMBED" "HEADER;CATALOG;PRODUCT;CODE" "LINK")
    foreach(_required HEADER CATALOG PRODUCT CODE)
        if(NOT _arg_${_required})
            message(FATAL_ERROR "anira_example(${name}): ${_required} is required")
        endif()
    endforeach()
    get_filename_component(_header "${_arg_HEADER}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    file(READ "${_arg_CATALOG}" _catalog)
    string(JSON _repo GET "${_catalog}" repo)
    get_filename_component(_repo "${_repo}" NAME)

    anira_models_fetch("${_arg_CATALOG}" OUT_DIR _weights OUT_FILES _files)

    add_library(${name}-example INTERFACE)
    target_compile_definitions(${name}-example INTERFACE
        ANIRA_EXAMPLE_HEADER="${_header}"
        ANIRA_EXAMPLE_ID="org.anira-project.${name}"
        ANIRA_EXAMPLE_WEIGHTS="${_repo}")
    target_link_libraries(${name}-example INTERFACE anira_examples_common ${_arg_LINK})
    if(_arg_EMBED)
        anira_models_embed(${name}-weights
            NAMESPACE anira::examples
            SOURCE_DIR "${_weights}"
            FILES ${_files})
        target_compile_definitions(${name}-example INTERFACE ANIRA_EXAMPLE_EMBEDDED=1)
        target_link_libraries(${name}-example INTERFACE ${name}-weights)
        set(_copy "")
    else()
        # A script that copies the weights into a bundle: -DDEST=<dir>.
        set(_copy "${CMAKE_CURRENT_BINARY_DIR}/${name}-copy-weights.cmake")
        set(_script "")
        foreach(_file IN LISTS _files)
            get_filename_component(_dir "${_file}" DIRECTORY)
            string(APPEND _script
                "file(MAKE_DIRECTORY \"\${DEST}/${_dir}\")\n"
                "file(COPY_FILE \"${_weights}/${_file}\" \"\${DEST}/${_file}\" ONLY_IF_DIFFERENT)\n")
        endforeach()
        file(WRITE "${_copy}" "${_script}")
    endif()

    if(ANIRA_EXAMPLES_WITH_JUCE)
        _anira_example_juce(${name} "${_arg_PRODUCT}" ${_arg_CODE} "${_repo}" "${_copy}")
    endif()
    if(ANIRA_EXAMPLES_WITH_CLAP)
        _anira_example_clap(${name} "${_arg_PRODUCT}" ${_arg_CODE} "${_repo}" "${_copy}")
    endif()
    if(ANIRA_EXAMPLES_WITH_STANDALONE)
        _anira_example_standalone(${name} "${_arg_PRODUCT}" "${_repo}" "${_copy}")
    endif()
endfunction()

# After <target> is built: its example's weights into <binary dir>/../Resources/models/<repo>.
function(_anira_example_copy_weights target repo copy)
    if(copy AND TARGET ${target})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                    "-DDEST=$<TARGET_FILE_DIR:${target}>/../Resources/models/${repo}"
                    -P "${copy}"
            VERBATIM)
    endif()
endfunction()

function(_anira_example_juce name product code repo copy)
    if(TANH_OPERATING_SYSTEM STREQUAL "macOS")
        set(_formats AU VST3 Standalone)
    else()
        set(_formats VST3 Standalone)
    endif()
    juce_add_plugin(${name}-juce
        PRODUCT_NAME "${product}"
        COMPANY_NAME "anira-project"
        BUNDLE_ID "org.anira-project.${name}.juce"
        PLUGIN_MANUFACTURER_CODE Anir
        PLUGIN_CODE ${code}
        FORMATS ${_formats}
        HARDENED_RUNTIME_ENABLED TRUE
        HARDENED_RUNTIME_OPTIONS "com.apple.security.device.audio-input"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "The standalone runs your audio input through the model")
    target_sources(${name}-juce PRIVATE
        ${_anira_examples_dir}/targets/juce-plugin/PluginProcessor.cpp
        ${_anira_examples_dir}/targets/juce-plugin/PluginEditor.cpp)
    target_compile_definitions(${name}-juce PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    if(TANH_OPERATING_SYSTEM STREQUAL "Linux")
        target_compile_definitions(${name}-juce PRIVATE JUCE_JACK=1)
    endif()
    target_link_libraries(${name}-juce
        PRIVATE
            ${name}-example
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    foreach(_format IN LISTS _formats)
        _anira_example_copy_weights(${name}-juce_${_format} "${repo}" "${copy}")
    endforeach()
endfunction()

function(_anira_example_clap name product code repo copy)
    add_library(${name}-clap-impl STATIC ${_anira_examples_dir}/targets/clap-plugin/Plugin.cpp)
    target_link_libraries(${name}-clap-impl PUBLIC ${name}-example clap clap-helpers)
    target_compile_definitions(${name}-clap-impl PUBLIC ANIRA_EXAMPLE_PRODUCT="${product}")
    make_clapfirst_plugins(
        TARGET_NAME ${name}-clap
        IMPL_TARGET ${name}-clap-impl
        OUTPUT_NAME "${product}"
        ENTRY_SOURCE ${_anira_examples_dir}/targets/clap-plugin/Entry.cpp
        BUNDLE_IDENTIFIER "org.anira-project.${name}.clap"
        BUNDLE_VERSION "${PROJECT_VERSION}"
        COPY_AFTER_BUILD FALSE
        PLUGIN_FORMATS CLAP VST3 AUV2
        ASSET_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap/${name}"
        AUV2_MANUFACTURER_NAME "anira-project"
        AUV2_MANUFACTURER_CODE Anir
        AUV2_SUBTYPE_CODE ${code}
        AUV2_INSTRUMENT_TYPE aufx)
    foreach(_format clap vst3 auv2)
        _anira_example_copy_weights(${name}-clap_${_format} "${repo}" "${copy}")
    endforeach()
endfunction()

function(_anira_example_standalone name product repo copy)
    add_executable(${name}-standalone MACOSX_BUNDLE
        ${_anira_examples_dir}/targets/standalone/Main.cpp)
    target_link_libraries(${name}-standalone PRIVATE ${name}-example anira_examples_standalone)
    set_target_properties(${name}-standalone PROPERTIES
        OUTPUT_NAME "${product}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.anira-project.${name}.standalone"
        MACOSX_BUNDLE_BUNDLE_NAME "${product}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST ${_anira_examples_dir}/targets/standalone/Info.plist.in)
    _anira_example_copy_weights(${name}-standalone "${repo}" "${copy}")
endfunction()
