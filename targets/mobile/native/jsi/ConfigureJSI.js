const {
    withPodfile,
    withAppBuildGradle,
    withProjectBuildGradle,
    withGradleProperties,
} = require("expo/config-plugins");

/**
 * Expo config plugin wiring the NativeAniraJSI C++ TurboModule into the
 * prebuild-generated ios/ and android/ projects:
 *
 * - iOS: adds the NativeAniraJSI pod (which CMake-builds anira + the audio
 *   backend as static libs in a pre-compile script phase)
 * - Android: points gradle's externalNativeBuild at native/jsi/CMakeLists.txt,
 *   which builds everything into libappmodules.so
 */

// Add NativeAniraJSI to the iOS Podfile
const withCustomPodfile = (config) => {
    return withPodfile(config, (config) => {
        const podfileContent = config.modResults.contents;

        if (!podfileContent.includes("pod 'NativeAniraJSI'")) {
            config.modResults.contents = podfileContent.replace(
                /(target '[^']+' do\s+use_expo_modules!)/,
                `$1\n  pod 'NativeAniraJSI', :path => '../native/jsi'`
            );
        }

        return config;
    });
};

// Add the CMake configuration to android/app/build.gradle
const withCustomAppBuildGradle = (config) => {
    return withAppBuildGradle(config, (config) => {
        let contents = config.modResults.contents;

        if (!contents.includes("externalNativeBuild")) {
            contents = contents.replace(
                /(buildTypes\s*\{[\s\S]*?\}\s*\})/,
                `$1\n    externalNativeBuild {\n        cmake {\n            path "../../native/jsi/CMakeLists.txt"\n            buildStagingDirectory "../../build/android"\n            version "3.31.6"\n        }\n    }`
            );
        }

        config.modResults.contents = contents;
        return config;
    });
};

// anira's prebuilt mobile backends (anira-project/backends) ship arm64-v8a and
// x86_64 only — drop React Native's default 32-bit ABIs.
const withSupportedAbis = (config) => {
    return withGradleProperties(config, (config) => {
        const abis = "arm64-v8a,x86_64";
        const existing = config.modResults.find(
            (item) => item.type === "property" && item.key === "reactNativeArchitectures"
        );
        if (existing) {
            existing.value = abis;
        } else {
            config.modResults.push({
                type: "property",
                key: "reactNativeArchitectures",
                value: abis,
            });
        }
        return config;
    });
};

// Upgrade Kotlin version in the root build.gradle (required for Gradle 9 / KSP)
const withKotlinVersionUpdate = (config) => {
    return withProjectBuildGradle(config, (config) => {
        config.modResults.contents = config.modResults.contents.replace(
            /kotlinVersion\s*=\s*"[^"]*"/,
            'kotlinVersion = "2.1.21"'
        );
        return config;
    });
};

module.exports = (config) => {
    config = withCustomPodfile(config);
    config = withCustomAppBuildGradle(config);
    config = withSupportedAbis(config);
    config = withKotlinVersionUpdate(config);

    return config;
};
