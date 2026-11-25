// Custom OnLoad.cpp — extends the default React Native setup with NativeAniraJSI.
// Must live next to the Android CMakeLists.txt so React Native's CMake skips
// its own default OnLoad.cpp.

#include <DefaultComponentsRegistry.h>
#include <DefaultTurboModuleManagerDelegate.h>
#include <FBReactNativeSpec.h>
#include <NativeAniraJSI.h>
#include <autolinking.h>
#include <fbjni/fbjni.h>
#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>

#ifdef __ANDROID__
#include <tanh/audio-io/AudioDeviceManager.h>
#endif

#ifdef REACT_NATIVE_APP_CODEGEN_HEADER
#include REACT_NATIVE_APP_CODEGEN_HEADER
#endif
#ifdef REACT_NATIVE_APP_COMPONENT_DESCRIPTORS_HEADER
#include REACT_NATIVE_APP_COMPONENT_DESCRIPTORS_HEADER
#endif

namespace facebook::react {

void registerComponents(std::shared_ptr<const ComponentDescriptorProviderRegistry> registry) {
#ifdef REACT_NATIVE_APP_COMPONENT_REGISTRATION
    REACT_NATIVE_APP_COMPONENT_REGISTRATION(registry);
#endif
    autolinking_registerProviders(registry);
}

std::shared_ptr<TurboModule> cxxModuleProvider(const std::string& name,
                                               const std::shared_ptr<CallInvoker>& jsInvoker) {
    if (name == NativeAniraJSI::kModuleName) { return std::make_shared<NativeAniraJSI>(jsInvoker); }

    return autolinking_cxxModuleProvider(name, jsInvoker);
}

std::shared_ptr<TurboModule> javaModuleProvider(const std::string& name,
                                                const JavaTurboModule::InitParams& params) {
#ifdef REACT_NATIVE_APP_MODULE_PROVIDER
    auto module = REACT_NATIVE_APP_MODULE_PROVIDER(name, params);
    if (module != nullptr) { return module; }
#endif

    if (auto module = FBReactNativeSpec_ModuleProvider(name, params)) { return module; }

    if (auto module = autolinking_ModuleProvider(name, params)) { return module; }

    return nullptr;
}

}  // namespace facebook::react

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    return facebook::jni::initialize(vm, [vm] {
        facebook::react::DefaultTurboModuleManagerDelegate::cxxModuleProvider =
            &facebook::react::cxxModuleProvider;
        facebook::react::DefaultTurboModuleManagerDelegate::javaModuleProvider =
            &facebook::react::javaModuleProvider;
        facebook::react::DefaultComponentsRegistry::registerComponentDescriptorsFromEntryPoint =
            &facebook::react::registerComponents;

#ifdef __ANDROID__
        // Hand the JVM to the audio layer for AAudio device enumeration via JNI.
        thl::AudioDeviceManager::set_java_vm(vm);
#endif
    });
}
