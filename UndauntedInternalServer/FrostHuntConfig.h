// Identity of the dedicated 1.4.4 Frost Gnasher pursuit. AGPL-3.0-only.
#pragma once
#include "DojoProbe.h"

namespace FrostHuntConfig {
using namespace SDK;
inline constexpr char PlayerHuntId[] = "Frost144_PlayerHunt_Pursuit_Gnasher";
inline constexpr char MatchmakerHuntId[] = "Frost144_MatchmakerHunt_Gnasher";
inline constexpr char IslandPath[] = "/Game/Maps/islands/1609/bella_jen_snow";
inline constexpr char MonsterPath[] = "/Game/Monsters/frost144/mcbeaver_frost_bp.mcbeaver_frost_bp_C";
inline bool IsDedicatedWorld(UWorld* world) {
    if (!DojoProbe::IsExpectedProcess() || !world || !world->GameState
        || world->GetName().find("bella_jen_snow") == std::string::npos) return false;
    auto* type = UObject::FindClassFast("ArchonGameState");
    return type && world->GameState->IsA(type)
        && static_cast<AArchonGameState*>(world->GameState)->MatchmakerHuntID.ToString() == MatchmakerHuntId;
}
inline bool IsDedicatedServer(UWorld* world, int port) {
    return port >= 8760 && port <= 8765 && IsDedicatedWorld(world)
        && world->AuthorityGameMode && world->AuthorityGameMode->HasAuthority();
}
}
