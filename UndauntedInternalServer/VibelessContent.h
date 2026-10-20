// Windows side of the content registry (ContentRegistry.h): the registry of this profile, read once.
// Location: DR_TABLES_DIR/native/registry.txt when DR_TABLES_DIR is set (game servers, which the deploy
// server starts), else <profile>/tables/native/registry.txt (the client). A missing file means no
// registrations (a core-only profile); a malformed line is reported and skipped. AGPL-3.0-only.
#pragma once
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <windows.h>
#include "ContentRegistry.h"
#include "ProfileRoot.h"

namespace Vibeless {
struct LoadedContent { ContentRegistry::Registry Registry; std::wstring Source; bool Found = false; };

inline std::filesystem::path ContentRegistryPath() {
    wchar_t tables[32768] = {};
    const DWORD length = GetEnvironmentVariableW(L"DR_TABLES_DIR", tables, 32768);
    if (length > 0 && length < 32768) return std::filesystem::path(tables) / L"native" / L"registry.txt";
    const auto& root = ProfileRoot::Root();
    return root.empty() ? std::filesystem::path() : std::filesystem::path(root) / L"tables" / L"native" / L"registry.txt";
}

inline const LoadedContent& ContentInfo() {
    static const LoadedContent loaded = [] {
        LoadedContent result;
        const auto path = ContentRegistryPath();
        if (path.empty()) return result;
        result.Source = path.wstring();
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) return result;  // no registrations
        result.Found = true;
        if (size > 256 * 1024) { result.Registry.Errors.push_back("content registry is larger than 256 KiB; ignored"); return result; }
        std::ifstream input(path, std::ios::binary);
        std::stringstream text;
        text << input.rdbuf();
        result.Registry = ContentRegistry::Parse(text.str());
        return result;
    }();
    return loaded;
}
inline const ContentRegistry::Registry& Content() { return ContentInfo().Registry; }
}
