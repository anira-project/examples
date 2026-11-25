# The AUv3 side of the app (iOS only): the AniraAudioUnit over the app's
# ExampleEngine as a static library. The app-extension target itself — the view
# controller, Info.plist and embedding into the app — is created in the
# prebuild-generated Xcode project by ConfigureAUv3.js and links this library.
add_library(anira_auv3 STATIC
    src/AniraAudioUnit.mm
)

target_include_directories(anira_auv3
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(anira_auv3
    PUBLIC
    processor_host
    "-framework AudioToolbox"
    "-framework AVFoundation"
)

target_compile_features(anira_auv3 PUBLIC cxx_std_20)
set_source_files_properties(src/AniraAudioUnit.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)

set_target_properties(anira_auv3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
