// Restore authored stats after the stock pursuit spawner's single-number override.
#pragma once
#include "OriginalStrengthPolicy.h"
#include "FrostEncounter.h"
#include "ThunderdeepHuntConfig.h"

namespace OriginalStrength {
using namespace SDK;
inline UWorld* LastWorld = nullptr;
inline OriginalStrengthPolicy::SpawnRestorer Restorer;
inline std::set<std::string> Reported;
inline void Tick(UWorld* world, UNetDriver* driver, int port) {
    if (world != LastWorld) { LastWorld = world; Restorer.Reset(); Reported.clear(); }
    if (!world || !driver || world->NetDriver != driver || driver->ServerConnection
        || (!FrostHuntConfig::IsDedicatedServer(world, port)
            && !ThunderdeepHuntConfig::IsDedicatedServer(world, port))) return;
    auto* mode = FrostEncounter::IslandMode(world);
    auto* state = FrostEncounter::State(world);
    if (!mode || !state || mode->Class->GetName() != "BPGM_Archon_Prototype_C"
        || mode->SpawnedBehemoths.Num() < 0 || mode->SpawnedBehemoths.Num() > 128) return;
    for (auto* monster : mode->SpawnedBehemoths) {
        if (!monster || !monster->Class || monster->IsDefaultObject() || monster->IsActorBeingDestroyed()) continue;
        const auto expected = OriginalStrengthPolicy::Expected(port, state->MatchmakerHuntID.ToString(), DojoProbe::ObjectPath(monster->Class));
        if (!expected) continue;
        const bool ready = monster->HasAuthority() && monster->Controller && monster->GetCurrentHealth() > 0
            && FrostEncounter::Contains(state->SpawnedBehemoths, monster);
        const auto key = std::to_string(monster->Index) + ":" + monster->GetFullName();
        const auto* defaults = static_cast<AArchonBehemoth*>(monster->Class->ClassDefaultObject);
        const float powerBefore = monster->GlobalPower, resistanceBefore = monster->GlobalResistance;
        const float healthBefore = monster->GetCurrentHealth(), maxHealthBefore = monster->GetMaxHealth();
        const auto result = Restorer.Apply(key, ready, expected, defaults, *monster);
        if (result == OriginalStrengthPolicy::Result::Skipped || !Reported.insert(key).second) continue;
        const bool applied = result == OriginalStrengthPolicy::Result::Restored;
        if (applied) monster->ForceNetUpdate();
        const bool verified = applied && OriginalStrengthPolicy::Matches(monster->GlobalPower, expected->Power)
            && OriginalStrengthPolicy::Matches(monster->GlobalResistance, expected->Resistance)
            && OriginalStrengthPolicy::Matches(monster->GetCurrentHealth(), healthBefore)
            && OriginalStrengthPolicy::Matches(monster->GetMaxHealth(), maxHealthBefore);
        std::ofstream out(std::filesystem::path(DojoProbe::Root()) / L"reports/original-strength.jsonl", std::ios::app);
        out << "{\"pid\":" << GetCurrentProcessId() << ",\"port\":" << port
            << ",\"class\":" << DojoProbe::Json(DojoProbe::ObjectPath(monster->Class))
            << ",\"result\":" << DojoProbe::Json(!applied ? "unexpected_class_defaults" : verified ? "restored" : "readback_mismatch")
            << ",\"powerBefore\":" << powerBefore << ",\"resistanceBefore\":" << resistanceBefore
            << ",\"power\":" << monster->GlobalPower << ",\"resistance\":" << monster->GlobalResistance
            << ",\"healthBefore\":" << healthBefore << ",\"health\":" << monster->GetCurrentHealth()
            << ",\"maxHealthBefore\":" << maxHealthBefore << ",\"maxHealth\":" << monster->GetMaxHealth() << "}\n";
    }
}
}
