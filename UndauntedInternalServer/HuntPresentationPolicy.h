#pragma once
#include "OriginalStrengthPolicy.h"

namespace HuntPresentationPolicy {
// An explicit match takes precedence over the broader player-hunt selection.
inline std::optional<OriginalStrengthPolicy::Stats> ForHunt(std::string_view player, std::string_view match) {
    if (!match.empty()) return OriginalStrengthPolicy::Authored(match);
    if (player == "Frost144_PlayerHunt_Pursuit_Gnasher") return OriginalStrengthPolicy::Authored("Frost144_MatchmakerHunt_Gnasher");
    if (player == "Thunderdeep144_PlayerHunt_Pursuit_Drask") return OriginalStrengthPolicy::Authored("Thunderdeep144_MatchmakerHunt_Drask");
    return {};
}
struct Targets { float Weapon, Armour; };
inline Targets Recommended(const OriginalStrengthPolicy::Stats& creature) {
    // Player weapons oppose the creature's resistance; armour opposes its power.
    return {creature.Resistance, creature.Power};
}
}
