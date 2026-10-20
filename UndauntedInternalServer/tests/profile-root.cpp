#include "../ProfileRootPolicy.h"
#include <cassert>
#include <iostream>
#include <set>

int main() {
    using namespace ProfileRootPolicy;
    std::set<std::wstring> markers;
    int lookups = 0;
    const auto exists = [&](const std::wstring& path) { ++lookups; return markers.count(path) > 0; };

    // A profile-shaped folder without a marker or DR_PROFILE_ROOT stays off.
    const auto unmarked = L"C:\\Games\\Lab\\game\\Archon\\Binaries\\Win64\\Dauntless-Win64-Shipping.exe";
    assert(Resolve(unmarked, L"", exists).empty());
    assert(lookups == 1);

    // Original installations and other layouts stay off.
    assert(Resolve(L"C:\\Games\\Dauntless-1.4.4\\Archon\\Binaries\\Win64\\Dauntless-Win64-Shipping.exe", L"", exists).empty());
    assert(Resolve(L"C:\\Games\\Dauntless\\Archon\\Binaries\\Win64\\Dauntless-Win64-Shipping.exe", L"", exists).empty());
    assert(Resolve(L"", L"", exists).empty());
    assert(Resolve(L"D:/P/game/Archon/Binaries/Win64/Other.exe", L"", exists).empty());

    // A prepared profile is recognized by its marker.
    const auto profile = L"D:\\Vibeless\\Profiles\\Main\\game\\Archon\\Binaries\\Win64\\Dauntless-Win64-Shipping.exe";
    assert(Resolve(profile, L"", exists).empty());
    markers.insert(L"D:/Vibeless/Profiles/Main/vibeless-profile.json");
    assert(Resolve(profile, L"", exists) == L"D:/Vibeless/Profiles/Main/");

    // DR_PROFILE_ROOT must name the folder the game actually runs from.
    const auto other = L"E:/X/game/Archon/Binaries/Win64/Dauntless-Win64-Shipping.exe";
    assert(Resolve(other, L"E:\\X", exists) == L"E:/X/");
    assert(Resolve(other, L"e:/x/", exists) == L"E:/X/");
    assert(Resolve(other, L"E:/Y", exists).empty());
    assert(Resolve(profile, L"E:/X", exists).empty());

    // Relative segments are refused rather than resolved.
    assert(Resolve(L"E:/X/../Y/game/Archon/Binaries/Win64/Dauntless-Win64-Shipping.exe", L"", [](const std::wstring&) { return true; }).empty());

    assert(UserDir(L"D:/Vibeless/Profiles/Main/", false, 0) == L"D:/Vibeless/Profiles/Main/user/client/");
    assert(UserDir(L"D:/Vibeless/Profiles/Main/", true, 8767) == L"D:/Vibeless/Profiles/Main/user/server-8767/");
    assert(UserDir(L"", true, 8767).empty());

    std::cout << "profile-root: all assertions passed\n";
    return 0;
}
