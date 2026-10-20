// The two backport hunts and the Test Arena as their packages register them. AGPL-3.0-only.
#pragma once
#include "../ContentRegistry.h"
inline const char* BackportRegistryText() {
    return "vibeless-registry 1\n"
        "hunt backport.deepfrost-gnasher Frost144_MatchmakerHunt_Gnasher player=Frost144_PlayerHunt_Pursuit_Gnasher"
        " world=/Game/Maps/islands/1609/bella_jen_snow creature=/Game/Monsters/frost144/mcbeaver_frost_bp.mcbeaver_frost_bp_C"
        " power=240 resistance=120 return=1\n"
        "hunt backport.thunderdeep-drask Thunderdeep144_MatchmakerHunt_Drask player=Thunderdeep144_PlayerHunt_Pursuit_Drask"
        " world=/Game/Maps/islands/1803/frida_moss_falls creature=/Game/Monsters/drask144/fulgar_alpha_bp.fulgar_alpha_bp_C"
        " power=420 resistance=330 return=1\n"
        "destination backport.test-arena TestArena144_MatchmakerHunt player=TestArena144_PlayerHunt"
        " world=/Game/Maps/islands/arenas/arena_ramsgate_00 mode=/Game/Blueprints/BPGM_Archon_Prototype.BPGM_Archon_Prototype_C return=1\n";
}
inline ContentRegistry::Registry BackportRegistry() { return ContentRegistry::Parse(BackportRegistryText()); }
