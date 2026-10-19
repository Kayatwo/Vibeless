#include "../OriginalStrengthPolicy.h"
#include <cassert>
#include <iostream>
#include <limits>
struct Actor {
    float GlobalPower, GlobalResistance;
    float Health = 40480, PartHealth = 2500;
    int PowerUpdates = 0, ResistUpdates = 0;
    bool PartsUpdated = false;
    void UpdateBasePowers() { ++PowerUpdates; }
    void UpdateBaseResists(bool parts) { ++ResistUpdates; PartsUpdated = parts; }
};
int main() {
    using namespace OriginalStrengthPolicy;
    constexpr auto frost = "Frost144_MatchmakerHunt_Gnasher";
    constexpr auto drask = "Thunderdeep144_MatchmakerHunt_Drask";
    constexpr auto frostClass = "/Game/Monsters/frost144/mcbeaver_frost_bp.mcbeaver_frost_bp_C";
    constexpr auto draskClass = "/Game/Monsters/drask144/fulgar_alpha_bp.fulgar_alpha_bp_C";
    for (int port : {0, 8759, 8766, 8767}) assert(!Expected(port, frost, frostClass));
    assert(!Expected(8760, "CR19_MatchmakerHunt_Beaver", frostClass));
    assert(!Expected(8760, drask, frostClass));
    assert(!Expected(8760, frost, draskClass));
    assert(!Expected(8760, frost, "/Game/Monsters/mcrollin/mcbeaver_normal_bp.mcbeaver_normal_bp_C"));
    SpawnRestorer restorer;
    for (int port = 8760; port <= 8765; ++port) {
        for (bool isFrost : {true, false}) {
            const auto expected = Expected(port, isFrost ? frost : drask, isFrost ? frostClass : draskClass);
            assert(expected);
            Actor defaults{isFrost ? 240.f : 420.f, isFrost ? 120.f : 330.f};
            Actor actor{isFrost ? 100.f : 50.f, isFrost ? 100.f : 50.f};
            const auto key = std::to_string(port) + (isFrost ? ":frost" : ":drask");
            assert(restorer.Apply(key, false, expected, &defaults, actor) == Result::Skipped);
            assert(actor.PowerUpdates == 0);
            assert(restorer.Apply(key, true, {}, &defaults, actor) == Result::Skipped);
            assert(restorer.Apply(key, true, expected, static_cast<Actor*>(nullptr), actor) == Result::InvalidDefaults);
            auto badDefaults = defaults;
            badDefaults.GlobalResistance = std::numeric_limits<float>::quiet_NaN();
            assert(restorer.Apply(key, true, expected, &badDefaults, actor) == Result::InvalidDefaults);
            badDefaults = defaults; badDefaults.GlobalPower = 100;
            assert(restorer.Apply(key, true, expected, &badDefaults, actor) == Result::InvalidDefaults);
            assert(actor.PowerUpdates == 0 && actor.ResistUpdates == 0);
            assert(restorer.Apply(key, true, expected, &defaults, actor) == Result::Restored);
            assert(actor.GlobalPower == defaults.GlobalPower && actor.GlobalResistance == defaults.GlobalResistance);
            assert(actor.Health == 40480 && actor.PartHealth == 2500);
            assert(actor.PowerUpdates == 1 && actor.ResistUpdates == 1 && actor.PartsUpdated);
            assert(defaults.PowerUpdates == 0 && defaults.ResistUpdates == 0);
            // Subsequent rage/aether buffs and damage must survive repeated ticks.
            actor.GlobalPower += 75; actor.GlobalResistance += 25; actor.Health -= 100;
            for (int tick = 0; tick < 1000; ++tick) assert(restorer.Apply(key, true, expected, &defaults, actor) == Result::Skipped);
            assert(actor.GlobalPower == defaults.GlobalPower + 75 && actor.GlobalResistance == defaults.GlobalResistance + 25);
            assert(actor.Health == 40380 && actor.PowerUpdates == 1 && actor.ResistUpdates == 1);
        }
    }
    restorer.Reset(); // A new world may reuse object names/indices.
    Actor defaults{240,120}, actor{100,100};
    assert(restorer.Apply("8760:frost", true, Expected(8760,frost,frostClass), &defaults, actor) == Result::Restored);
    std::cout << "PASS original 240/120 and 420/330 defaults, hunt/class/port scope, deferred initialization, one-time updates, health preservation, 12000 combat ticks and world reset\n";
}
