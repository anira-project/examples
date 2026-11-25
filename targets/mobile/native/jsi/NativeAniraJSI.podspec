Pod::Spec.new do |s|
  s.name         = "NativeAniraJSI"
  s.version      = "0.1.0"
  s.summary      = "anira mobile example TurboModule"
  s.description  = "C++ TurboModule bridging React Native and the anira real-time neural network inference library"
  s.homepage     = "https://github.com/anira-project/examples"
  s.license      = "Apache-2.0"
  s.author       = { "Valentin Ackva" => "valentin.ackva@gmail.com" }
  s.platforms    = { :ios => "16.4" }
  s.source       = { :git => "local", :tag => "#{s.version}" }

  # The pod compiles only the thin JSI wrapper. The heavy C++ (anira, models'
  # example and its model, tanh-lib's AudioIO, the engines) is built by the CMake script
  # phase below into one static library, libanira_mobile.a, linked via
  # OTHER_LDFLAGS; its headers and compile definitions are staged beside it.
  s.source_files = [
    "src/**/*.{cpp,mm}",
    "include/**/*.{h,hpp}"
  ]

  s.public_header_files = "include/**/*.h"

  # BUILD_DIR layout is produced by the script phase: "staging" holds the
  # library, the headers and anira_mobile_defines.h (relative to the app root).
  build_dir = "build/ios-$(PLATFORM_NAME)/$(CONFIGURATION)"

  header_search_paths = [
    "$(PODS_TARGET_SRCROOT)/include",
    "$(PODS_TARGET_SRCROOT)/../../#{build_dir}/staging/include",
    "$(PODS_ROOT)/build/generated/ios",
    "$(PODS_ROOT)/ReactNativeDependencies"
  ].join(" ")

  # PODS_TARGET_SRCROOT = <app>/native/jsi for the pod itself;
  # PODS_ROOT = <app>/ios/Pods for the user project including the pod.
  library_search_paths_pod = [
    "$(PODS_TARGET_SRCROOT)/../../#{build_dir}/staging/lib"
  ].join(" ")

  library_search_paths_user = [
    "$(PODS_ROOT)/../../#{build_dir}/staging/lib"
  ].join(" ")

  ldflags = "-lanira_mobile"

  # React Native TurboModule dependencies
  s.dependency "React-Core"
  s.dependency "React-callinvoker"
  s.dependency "ReactCommon/turbomodule/core"
  s.dependency "ReactCodegen"
  s.dependency "ReactNativeDependencies"

  s.pod_target_xcconfig = {
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++20",
    "HEADER_SEARCH_PATHS" => "$(inherited) " + header_search_paths,
    # anira_mobile_defines.h: the definitions the native libraries were built with.
    "OTHER_CPLUSPLUSFLAGS" => "$(inherited) -DFOLLY_NO_CONFIG -DFOLLY_MOBILE=1 -DFOLLY_USE_LIBCPP=1 -DFOLLY_CFG_NO_COROUTINES=1 -DFOLLY_HAVE_CLOCK_GETTIME=1 -include anira_mobile_defines.h -x objective-c++",
    "LIBRARY_SEARCH_PATHS" => "$(inherited) " + library_search_paths_pod,
    "OTHER_LDFLAGS" => "$(inherited) " + ldflags,
    # The prebuilt ONNX Runtime xcframework ships arm64-only slices
    # (device and simulator) — no Intel simulator support.
    "ARCHS" => "arm64",
    "EXCLUDED_ARCHS[sdk=iphonesimulator*]" => "i386 x86_64"
  }

  s.user_target_xcconfig = {
    "LIBRARY_SEARCH_PATHS" => "$(inherited) " + library_search_paths_user,
    "OTHER_LDFLAGS" => "$(inherited) " + ldflags,
    "ARCHS" => "arm64",
    "EXCLUDED_ARCHS[sdk=iphonesimulator*]" => "i386 x86_64"
  }

  s.static_framework = true

  # Audio + ONNX Runtime (static) system dependencies
  s.frameworks = 'AVFoundation', 'AudioToolbox', 'CoreAudio', 'CoreMIDI', 'Accelerate', 'Foundation', 'CoreFoundation', 'Metal'

  s.script_phases = [
    {
      :name => 'Build anira backend with CMake',
      # Shared with the AUv3 app extension (native/auv3/ConfigureAUv3.js)
      :script => '"${PODS_TARGET_SRCROOT}/../scripts/build-ios-libs.sh"',
      :execution_position => :before_compile,
      :show_env_vars_in_log => true
      # No input/output files defined so that the script always runs
    }
  ]

end
