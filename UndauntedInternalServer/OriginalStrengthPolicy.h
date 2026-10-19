#pragma once
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace OriginalStrengthPolicy {
struct Stats { float Power, Resistance; };
inline std::optional<Stats> Authored(std::string_view hunt) {
    if (hunt == "Frost144_MatchmakerHunt_Gnasher") return Stats{240, 120};
    if (hunt == "Thunderdeep144_MatchmakerHunt_Drask") return Stats{420, 330};
    return {};
}
inline std::optional<Stats> Expected(int port, std::string_view hunt, std::string_view actorClass) {
    if (port < 8760 || port > 8765) return {};
    if (hunt == "Frost144_MatchmakerHunt_Gnasher"
        && actorClass == "/Game/Monsters/frost144/mcbeaver_frost_bp.mcbeaver_frost_bp_C") return Authored(hunt);
    if (hunt == "Thunderdeep144_MatchmakerHunt_Drask"
        && actorClass == "/Game/Monsters/drask144/fulgar_alpha_bp.fulgar_alpha_bp_C") return Authored(hunt);
    return {};
}
inline bool Matches(float actual, float expected) {
    return std::isfinite(actual) && std::fabs(actual - expected) < 0.001f;
}
enum class Result { Skipped, InvalidDefaults, Restored };
class SpawnRestorer {
    std::set<std::string> Restored;
public:
    void Reset() { Restored.clear(); }
    // Run only after authoritative native spawn initialization. Never refresh
    // an initialized actor, so later combat buffs and resistance changes survive.
    template<class Actor> Result Apply(const std::string& key, bool ready,
        std::optional<Stats> expected, const Actor* defaults, Actor& actor) {
        if (!ready || !expected || Restored.count(key)) return Result::Skipped;
        if (!defaults || !Matches(defaults->GlobalPower, expected->Power)
            || !Matches(defaults->GlobalResistance, expected->Resistance)) return Result::InvalidDefaults;
        Restored.insert(key); // Mark before entering engine callbacks.
        actor.GlobalPower = defaults->GlobalPower;
        actor.GlobalResistance = defaults->GlobalResistance;
        actor.UpdateBasePowers();
        actor.UpdateBaseResists(true);
        return Result::Restored;
    }
};
}
