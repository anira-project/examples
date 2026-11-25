const { withXcodeProject } = require("expo/config-plugins");

/**
 * Expo config plugin adding the AUv3 app extension to the prebuild-generated
 * iOS project. The extension is a dependency of the app target and embedded
 * into it (PlugIns/AniraAUv3.appex), so every app build — `expo run:ios`,
 * xcodebuild, CI — builds and bundles the plugin; installing the app
 * registers the audio unit with iOS plugin hosts.
 *
 * The Xcode target compiles only the view controller. The audio unit itself
 * (libanira_mobile.a) and everything below it come from the same CMake build the
 * NativeAniraJSI pod uses (native/scripts/build-ios-libs.sh).
 */

const TARGET_NAME = "AniraAUv3";
const EXTENSION_DIR = "../native/auv3/extension"; // relative to ios/
const SOURCES = ["AniraAudioUnitViewController.m"];
const HEADERS = ["AniraAudioUnitViewController.h"];

const BUILD_DIR = "$(SRCROOT)/../build/ios-$(PLATFORM_NAME)/$(CONFIGURATION)";

// The same library as the pod (see NativeAniraJSI.podspec), the AUv3 included.
const LIBRARIES = ["anira_mobile", "c++"];
const FRAMEWORKS = [
    "AudioToolbox",
    "AVFoundation",
    "CoreAudio",
    "CoreAudioKit",
    "CoreMIDI",
    "Accelerate",
    "Foundation",
    "CoreFoundation",
    "Metal",
    "UIKit",
    "QuartzCore",
];

const quote = (value) => `"${value}"`;

const withAUv3Target = (config) => {
    return withXcodeProject(config, (config) => {
        const project = config.modResults;

        // `expo prebuild` without --clean runs on an existing project
        // (addTarget() stores the name quoted)
        if (project.pbxTargetByName(TARGET_NAME) || project.pbxTargetByName(quote(TARGET_NAME))) {
            return config;
        }

        // addTarget() expects these sections; a fresh Expo project has none
        const objects = project.hash.project.objects;
        objects.PBXTargetDependency = objects.PBXTargetDependency || {};
        objects.PBXContainerItemProxy = objects.PBXContainerItemProxy || {};

        // Creates the target, makes it a dependency of the app target and
        // embeds the .appex into the app ("Copy Files" -> PlugIns).
        const bundleId = `${config.ios.bundleIdentifier}.${TARGET_NAME}`;
        const target = project.addTarget(TARGET_NAME, "app_extension", TARGET_NAME, bundleId);

        const group = project.addPbxGroup(
            [...SOURCES, ...HEADERS, "Info.plist"],
            TARGET_NAME,
            EXTENSION_DIR
        );
        const mainGroup = project.getFirstProject().firstProject.mainGroup;
        project.getPBXGroupByKey(mainGroup).children.push({
            value: group.uuid,
            comment: TARGET_NAME,
        });

        // Build phases run in the order they are added: CMake first
        project.addBuildPhase(
            [],
            "PBXShellScriptBuildPhase",
            "Build anira backend with CMake",
            target.uuid,
            {
                shellPath: "/bin/sh",
                shellScript: '"${SRCROOT}/../native/scripts/build-ios-libs.sh"\n',
            }
        );
        project.addBuildPhase(SOURCES, "PBXSourcesBuildPhase", "Sources", target.uuid);
        project.addBuildPhase([], "PBXFrameworksBuildPhase", "Frameworks", target.uuid);
        project.addBuildPhase([], "PBXResourcesBuildPhase", "Resources", target.uuid);

        // The audio unit's name and subtype: app.json's expo.extra.anira (the app's example).
        const anira = config.extra?.anira ?? {};
        const buildSettings = {
            ANIRA_AU_NAME: quote(anira.auName ?? "anira example"),
            ANIRA_AU_DESCRIPTION: quote(anira.auDescription ?? "anira example"),
            ANIRA_AU_SUBTYPE: quote(anira.auSubtype ?? "anex"),
            INFOPLIST_FILE: quote(`${EXTENSION_DIR}/Info.plist`),
            GENERATE_INFOPLIST_FILE: "NO",
            // An extension must carry the version of its containing app
            MARKETING_VERSION: quote(config.version ?? "1.0.0"),
            CURRENT_PROJECT_VERSION: quote(config.ios.buildNumber ?? "1"),
            IPHONEOS_DEPLOYMENT_TARGET: "16.4",
            TARGETED_DEVICE_FAMILY: quote("1,2"),
            CLANG_ENABLE_OBJC_ARC: "YES",
            CLANG_ENABLE_MODULES: "YES",
            APPLICATION_EXTENSION_API_ONLY: "YES",
            CODE_SIGN_STYLE: "Automatic",
            // The script phase writes outside the target's declared outputs
            ENABLE_USER_SCRIPT_SANDBOXING: "NO",
            // anira's prebuilt backends are arm64-only (device and simulator)
            ARCHS: "arm64",
            [quote("EXCLUDED_ARCHS[sdk=iphonesimulator*]")]: quote("i386 x86_64"),
            HEADER_SEARCH_PATHS: [quote("$(inherited)"), quote("$(SRCROOT)/../native/auv3/include")],
            LIBRARY_SEARCH_PATHS: [quote("$(inherited)"), quote(`${BUILD_DIR}/staging/lib`)],
            OTHER_LDFLAGS: [
                quote("$(inherited)"),
                ...LIBRARIES.map((library) => quote(`-l${library}`)),
                ...FRAMEWORKS.flatMap((framework) => [quote("-framework"), quote(framework)]),
            ],
        };
        if (config.ios.appleTeamId) {
            buildSettings.DEVELOPMENT_TEAM = config.ios.appleTeamId;
        }

        const configurations = project.pbxXCBuildConfigurationSection();
        for (const key of Object.keys(configurations)) {
            const settings = configurations[key].buildSettings;
            if (settings && settings.PRODUCT_NAME === quote(TARGET_NAME)) {
                Object.assign(settings, buildSettings);
            }
        }

        return config;
    });
};

module.exports = withAUv3Target;
