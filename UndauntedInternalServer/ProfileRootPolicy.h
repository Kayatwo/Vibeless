// Which installation the Vibeless features belong to. AGPL-3.0-only.
//
// The features (backport creatures, Test Arena, diagnostics) run only inside a prepared profile:
// <root>/game/Archon/Binaries/Win64/Dauntless-Win64-Shipping.exe, where <root> is
//   - a folder holding vibeless-profile.json, or
//   - the folder named by DR_PROFILE_ROOT (it must be the same folder the game runs from).
// Anything else, such as an unmodified game folder, keeps the features off. Portable: no Windows calls.
#pragma once
#include <cwctype>
#include <functional>
#include <string>
#include <string_view>

namespace ProfileRootPolicy {
inline constexpr std::wstring_view ExeSuffix = L"/game/Archon/Binaries/Win64/Dauntless-Win64-Shipping.exe";
inline constexpr std::wstring_view MarkerFile = L"vibeless-profile.json";

inline std::wstring Slashes(std::wstring_view path) {
    std::wstring out(path);
    for (auto& c : out) if (c == L'\\') c = L'/';
    return out;
}
inline bool SameText(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    return true;
}
inline std::wstring WithSlash(std::wstring path) {
    if (path.empty() || path.back() != L'/') path += L'/';
    return path;
}

// Returns the profile root with forward slashes and a trailing '/', or an empty string when the
// features must stay off. markerExists receives the full path of the marker file.
inline std::wstring Resolve(std::wstring_view exePath, std::wstring_view envRoot,
    const std::function<bool(const std::wstring&)>& markerExists) {
    const auto exe = Slashes(exePath);
    if (exe.size() <= ExeSuffix.size()) return {};
    const auto suffixAt = exe.size() - ExeSuffix.size();
    if (!SameText(std::wstring_view(exe).substr(suffixAt), ExeSuffix)) return {};
    const auto root = WithSlash(exe.substr(0, suffixAt));
    if (root.find(L"/../") != std::wstring::npos || root.find(L"/./") != std::wstring::npos) return {};
    if (!envRoot.empty()) return SameText(WithSlash(Slashes(envRoot)), root) ? root : std::wstring{};
    return markerExists && markerExists(root + std::wstring(MarkerFile)) ? root : std::wstring{};
}

// -UserDir for a client (port 0) or a game server, inside the profile.
inline std::wstring UserDir(const std::wstring& root, bool server, int port) {
    if (root.empty()) return {};
    return server ? root + L"user/server-" + std::to_wstring(port) + L"/" : root + L"user/client/";
}
}
