// Authored strength restoration rules (shared hunt services). Which hunts and creatures it applies to
// comes from the content registry (ContentRegistry.h), not from this file. AGPL-3.0-only.
#pragma once
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include "ContentRegistry.h"
#include "HuntPortPolicy.h"

namespace OriginalStrengthPolicy {
using Stats = ContentRegistry::Strength;
// The strength a spawned creature must get back: only on hunt servers, only for a registered hunt
// and exactly its registered creature class.
inline std::optional<Stats> Expected(const ContentRegistry::Registry& registry, int port,
    std::string_view hunt, std::string_view actorClass) {
    if (!HuntPortPolicy::IsHunt(port)) return {};
    return registry.AuthoredFor(hunt, actorClass);
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
