// The profile root of this game process (Windows side of ProfileRootPolicy.h). AGPL-3.0-only.
// Shared by the core runtime (UserDir) and the Vibeless features (diagnostics, markers).
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <windows.h>
#include "ProfileRootPolicy.h"

namespace ProfileRoot {
// The profile root with a trailing '/', or empty outside a Vibeless profile.
inline const std::wstring& Root() {
    static const std::wstring root = [] {
        wchar_t exe[32768]{};
        if (!GetModuleFileNameW(nullptr, exe, 32768)) return std::wstring{};
        wchar_t env[32768]{};
        const DWORD length = GetEnvironmentVariableW(L"DR_PROFILE_ROOT", env, 32768);
        const std::wstring_view envRoot(env, length < 32768 ? length : 0);
        return ProfileRootPolicy::Resolve(exe, envRoot, [](const std::wstring& marker) {
            std::error_code error;
            return std::filesystem::is_regular_file(std::filesystem::path(marker), error);
        });
    }();
    return root;
}
}
