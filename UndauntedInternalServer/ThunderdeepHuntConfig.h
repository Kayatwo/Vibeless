// Identity of the dedicated private 1.4.4 Thunderdeep pursuit. AGPL-3.0-only.
#pragma once
#include "DojoProbe.h"
#include "HuntPortPolicy.h"

namespace ThunderdeepHuntConfig {
using namespace SDK;
inline constexpr char PlayerHuntId[] = "Thunderdeep144_PlayerHunt_Pursuit_Drask";
inline constexpr char MatchmakerHuntId[] = "Thunderdeep144_MatchmakerHunt_Drask";
inline constexpr char IslandPath[] = "/Game/Maps/islands/1803/frida_moss_falls";
inline constexpr char MonsterPath[] = "/Game/Monsters/drask144/fulgar_alpha_bp.fulgar_alpha_bp_C";
inline bool IsDedicatedWorld(UWorld* world) {
    if (!DojoProbe::IsExpectedProcess() || !world || !world->GameState
        || DojoProbe::ObjectPath(world) != "/Game/Maps/islands/1803/frida_moss_falls.frida_moss_falls") return false;
    auto* type = UObject::FindClassFast("ArchonGameState");
    return type && world->GameState->IsA(type)
        && static_cast<AArchonGameState*>(world->GameState)->MatchmakerHuntID.ToString() == MatchmakerHuntId;
}
inline bool IsDedicatedServer(UWorld* world, int port) {
    return HuntPortPolicy::IsHunt(port) && IsDedicatedWorld(world)
        && world->AuthorityGameMode && world->AuthorityGameMode->HasAuthority();
}
}
