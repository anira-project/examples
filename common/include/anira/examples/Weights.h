// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/models/core/Error.h>
#include <anira/models/core/ModelSource.h>

#include <filesystem>
#include <string>

#ifdef ANIRA_EXAMPLE_EMBEDDED
#include <EmbeddedModels.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace anira::examples {

/// The directory of the binary this code is linked into (the plugin, not the host).
inline std::filesystem::path module_directory() {
#ifdef _WIN32
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&module_directory),
        &module);
    std::wstring path(MAX_PATH, L'\0');
    path.resize(GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size())));
    return std::filesystem::path(path).parent_path();
#else
    Dl_info info{};
    dladdr(reinterpret_cast<const void*>(&module_directory), &info);
    return std::filesystem::path(info.dli_fname != nullptr ? info.dli_fname : "").parent_path();
#endif
}

/// The example's weights (anira_example in cmake/examples.cmake): compiled into the binary, or
/// copied into its bundle, <binary dir>/../Resources/models/<repository>. Throws
/// core::Error when they are missing.
inline anira::models::core::ModelSource weights() {
#ifdef ANIRA_EXAMPLE_EMBEDDED
    return embedded_models();
#else
    const std::filesystem::path dir =
        module_directory() / ".." / "Resources" / "models" / ANIRA_EXAMPLE_WEIGHTS;
    if (!std::filesystem::is_directory(dir)) {
        throw anira::models::core::Error("the weights are missing: " + dir.string());
    }
    return anira::models::core::ModelSource::directory(dir.lexically_normal());
#endif
}

}  // namespace anira::examples
