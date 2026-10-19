// Adjustable foundation behaviour (Phase 0 decision D6). AGPL-3.0-only.
//
// Read once from the process environment, which the launcher or service runner sets per profile.
// Client and game servers of one profile must use the same values for DR_CELL_FUSION_HOURS
// (the client shows the time, the server builds the fusion's end time from it).
//
//   DR_CELL_FUSION_HOURS  1 to 720: every Middleman fusion recipe takes that many hours (default 1).
//                         retail (or 0): keep the cooked 1.4.4 times (22 h, 46 h, 70 h).
//   DR_AUTO_PRESS_START   on/off: press the 1.4.4 welcome screen's OK automatically (default on).
//   DR_DUALSENSE          on/off: read a PS5 DualSense as an Xbox pad and show PS5 prompts (default on).
//
// on/off accept 1/0, true/false, on/off, yes/no in any case; anything else keeps the default.
// The parsers are portable; only the environment read uses Windows.
#pragma once
#include <cwchar>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#endif

namespace FoundationSettings {
inline constexpr int DefaultFusionHours = 1;
inline constexpr int RetailFusionHours = 0;

inline std::wstring Lower(std::wstring_view text) {
    std::wstring out;
    for (const auto c : text)
        if (!std::iswspace(c)) out += static_cast<wchar_t>(std::towlower(c));
    return out;
}

inline bool ParseOnOff(std::wstring_view text, bool fallback) {
    const auto value = Lower(text);
    if (value == L"1" || value == L"true" || value == L"on" || value == L"yes") return true;
    if (value == L"0" || value == L"false" || value == L"off" || value == L"no") return false;
    return fallback;
}

// Hours for every fusion recipe, or RetailFusionHours to leave the cooked times alone.
inline int ParseFusionHours(std::wstring_view text) {
    const auto value = Lower(text);
    if (value.empty()) return DefaultFusionHours;
    if (value == L"retail" || value == L"0") return RetailFusionHours;
    wchar_t* end = nullptr;
    const long hours = std::wcstol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end || hours < 1 || hours > 720) return DefaultFusionHours;
    return static_cast<int>(hours);
}

#ifdef _WIN32
inline std::wstring Environment(const wchar_t* name) {
    wchar_t buffer[64] = {};
    const DWORD length = GetEnvironmentVariableW(name, buffer, 64);
    return length > 0 && length < 64 ? std::wstring(buffer, length) : std::wstring{};
}
inline int FusionHours() {
    static const int hours = ParseFusionHours(Environment(L"DR_CELL_FUSION_HOURS"));
    return hours;
}
inline bool AutoPressStart() {
    static const bool enabled = ParseOnOff(Environment(L"DR_AUTO_PRESS_START"), true);
    return enabled;
}
inline bool DualSense() {
    static const bool enabled = ParseOnOff(Environment(L"DR_DUALSENSE"), true);
    return enabled;
}
#endif
}
