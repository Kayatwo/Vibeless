// Generic access to the hunt world's game mode, game state and spawned-behemoth lists.
// Shared creature/hunt services use these; no creature-specific logic belongs here. AGPL-3.0-only.
#pragma once
#include "SDK.hpp"
#include "SDK/BPGS_Archon_GameState_classes.hpp"

namespace HuntWorld {
using namespace SDK;
inline bool Contains(const TArray<AArchonBehemoth*>& array, AArchonBehemoth* behemoth) {
    if (array.Num() < 0 || array.Num() > 128) return false;
    for (auto* entry : array) if (entry == behemoth) return true;
    return false;
}
inline ABPGS_Archon_GameState_C* State(UWorld* world) {
    auto* type = UObject::FindClassFast("BPGS_Archon_GameState_C");
    return world && world->GameState && type && world->GameState->IsA(type)
        ? static_cast<ABPGS_Archon_GameState_C*>(world->GameState) : nullptr;
}
inline AArchonGameMode_Island* IslandMode(UWorld* world) {
    auto* type = UObject::FindClassFast("ArchonGameMode_Island");
    return world && world->AuthorityGameMode && type && world->AuthorityGameMode->IsA(type)
        ? static_cast<AArchonGameMode_Island*>(world->AuthorityGameMode) : nullptr;
}
}
