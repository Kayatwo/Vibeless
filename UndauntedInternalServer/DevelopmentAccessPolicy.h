#pragma once
#include <string>
#include <string_view>

// Legacy external controls and the Training Grounds replacement stay parked.
// The permanent TestArena144 destination has its own world-scoped controls.
namespace DevelopmentAccessPolicy {
inline constexpr bool Enabled = false;

inline std::wstring StockMapURL(std::wstring_view map, std::wstring_view monster,
    std::wstring_view hunt, std::wstring_view players) {
    std::wstring url(map);
    if (monster.find(L"NO_BEHEMOTH") == std::wstring_view::npos) url += L"?MonsterClass=" + std::wstring(monster);
    if (hunt.find(L"NO_MM_HUNTID") == std::wstring_view::npos) url += L"?HuntId=" + std::wstring(hunt);
    if (players.find(L"NO_EXPECTED_PLAYERS") == std::wstring_view::npos) url += L"?PlayerHuntIds=" + std::wstring(players);
    return url;
}
}
