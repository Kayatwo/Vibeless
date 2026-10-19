#pragma once
#include <string_view>

namespace EquipmentProbePolicy {
struct ScopeInput {
    bool expectedProcess;
    bool validWorldDriver;
    bool serverConnection;
    bool authorityMode;
    bool modeHasAuthority;
    bool openTestArena;
    int port;
    std::string_view mapPath;
    std::string_view modeName;
};
inline bool InScope(const ScopeInput& input) {
    return input.expectedProcess && input.validWorldDriver && !input.serverConnection
        && input.authorityMode && input.modeHasAuthority && input.openTestArena && input.port == 8766
        && input.mapPath == "/Game/Maps/islands/arenas/arena_ramsgate_00.arena_ramsgate_00"
        && input.modeName == "BPGM_Archon_Prototype_C";
}
inline bool MayLoad(const ScopeInput& input, int connections) {
    return InScope(input) && connections == 0;
}
}
