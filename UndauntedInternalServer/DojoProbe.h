// Isolated Local144 developer arena. AGPL-3.0-only.
#pragma once
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "SDK/bp_gamemode_behemoth_spawner_component_parameters.hpp"
#include "SDK/bp_gamemode_behemoth_spawner_component_classes.hpp"
#include "SDK/BP_PatrolCenterPoint_classes.hpp"
#include "SDK/NavigationSystem_classes.hpp"

namespace DojoProbe {
using namespace SDK;
inline constexpr wchar_t Root[] = L"C:/Dauntless/Experiments/Dojo144/";
inline constexpr char DefaultMonster[] = "/Game/Monsters/mcrollin/mcbeaver_normal_bp.mcbeaver_normal_bp_C";
inline ULONGLONG NextPoll = 0, NextSample = 0;
inline std::set<std::string> ConsumedRequests;
inline std::string LastStatus = "starting", LastRequestId, LastRequestResult;
inline UWorld* LastWorld = nullptr;
struct TrackedMonster {
    AArchonBehemoth* Actor;
    int32 Index;
    std::string RequestId;
    bool DeathReported = false;
    std::string SpawnMethod, SpawnPoint;
    FVector SpawnLocation{};
    bool OwnAgentProjected = false;
};
inline std::vector<TrackedMonster> Monsters;
struct SpawnRequest { std::string Id; std::string MonsterClass = DefaultMonster; float Distance = 1000.0f; };

inline std::string Json(const std::string& value) {
    std::string result = "\"";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { result += '\\'; result += static_cast<char>(ch); }
        else if (ch == '\n') result += "\\n";
        else if (ch == '\r') result += "\\r";
        else if (ch == '\t') result += "\\t";
        else if (ch >= 32) result += static_cast<char>(ch);
    }
    return result + "\"";
}
inline const char* Bool(bool value) { return value ? "true" : "false"; }
inline std::string ObjectPath(UObject* object) {
    if (!object) return {};
    auto* package = object;
    while (package->Outer) package = package->Outer;
    return package->Name.GetRawString() + "." + object->GetName();
}
inline void Log(const std::string& body) {
    std::ofstream out(std::filesystem::path(Root) / L"reports/dojo-probe.jsonl", std::ios::app);
    out << "{\"uptimeMs\":" << GetTickCount64() << "," << body << "}\n";
}
inline void AtomicJson(const wchar_t* name, const std::string& body) {
    const auto destination = std::filesystem::path(Root) / L"reports" / name;
    const auto temporary = std::filesystem::path(destination.wstring() + L".tmp");
    { std::ofstream out(temporary); out << body << '\n'; }
    MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING);
}
inline void Status(const std::string& value) {
    if (LastStatus != value) { LastStatus = value; Log("\"event\":\"status\",\"state\":" + Json(value)); }
}
inline void Result(const SpawnRequest& request, const std::string& status, const std::string& reason = "") {
    LastRequestId = request.Id;
    LastRequestResult = status;
    AtomicJson(L"spawn-result.json", "{\"requestId\":" + Json(request.Id) + ",\"status\":" + Json(status)
        + ",\"reason\":" + Json(reason) + ",\"monsterClass\":" + Json(request.MonsterClass)
        + ",\"uptimeMs\":" + std::to_string(GetTickCount64()) + "}");
    Log("\"event\":\"request_result\",\"requestId\":" + Json(request.Id) + ",\"status\":" + Json(status) + ",\"reason\":" + Json(reason));
    // The local control service queues only while this path is absent.
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(Root) / L"data/spawn.request", ec);
    NextSample = 0;
}
inline std::string Trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
struct ArenaConfiguration { std::wstring URL; bool FrostHunt = false; };
inline const ArenaConfiguration& ConfiguredArena() {
    static const ArenaConfiguration configured = [] {
        std::string map = "/Game/Maps/islands/1705/dia_moss_triforce", mode = "hunt";
        std::string playerId;
        std::ifstream input(std::filesystem::path(Root) / L"data/dev-map.ini", std::ios::binary);
        std::string line;
        while (std::getline(input, line)) {
            const auto equals = line.find('=');
            if (equals == std::string::npos) continue;
            const auto key = Trim(line.substr(0, equals)), value = Trim(line.substr(equals + 1));
            if (key == "MapPath") map = value;
            else if (key == "GameMode") mode = value;
            else if (key == "PlayerUserId") playerId = value;
        }
        if (map != "/Game/Maps/islands/1705/dia_moss_triforce"
            && map != "/Game/Maps/islands/dojo/training_dojo_persistent"
            && map != "/Game/Maps/islands/arenas/arena_ramsgate_00") map = "/Game/Maps/islands/1705/dia_moss_triforce";
        const bool frost = mode == "frost-hunt" && map != "/Game/Maps/islands/dojo/training_dojo_persistent"
            && playerId.size() == 40 && playerId.starts_with("UID-")
            && std::all_of(playerId.begin(), playerId.end(), [](unsigned char ch) { return std::isalnum(ch) || ch == '-'; });
        auto url = map + (mode == "dojo"
            ? "?game=/Game/Blueprints/GameMode/BPGM_ArchonTrainingGrounds.BPGM_ArchonTrainingGrounds_C"
            : "?game=/Game/Blueprints/BPGM_Archon_Prototype.BPGM_Archon_Prototype_C") + "?MaxPlayers=1?ZonePreset=0";
        if (frost) url += "?MonsterClass=/Game/Monsters/frost144/mcbeaver_frost_bp.mcbeaver_frost_bp_C"
            "?HuntId=CR19_MatchmakerHunt_Beaver?PlayerHuntIds=" + playerId + ":CR19_PlayerHunt_Pursuit_Beaver";
        return ArenaConfiguration{std::wstring(url.begin(), url.end()), frost};
    }();
    return configured;
}
inline std::wstring ConfiguredMapURL() { return ConfiguredArena().URL; }
inline bool FrostHuntEnabled() { return ConfiguredArena().FrostHunt; }
inline UFunction* FindFunction(UObject* object, const std::string& name) {
    for (UStruct* type = object ? object->Class : nullptr; type; type = type->SuperStruct)
        for (UField* field = type->Children; field; field = field->Next)
            if (field->HasTypeFlag(EClassCastFlags::Function) && field->GetName() == name) return static_cast<UFunction*>(field);
    return nullptr;
}
inline bool IsExpectedProcess() {
    static const bool valid = [] {
        wchar_t exe[32768]{};
        if (!GetModuleFileNameW(nullptr, exe, 32768)) return false;
        return !_wcsicmp(exe, L"C:\\Dauntless\\Experiments\\Dojo144\\game\\Archon\\Binaries\\Win64\\Dauntless-Win64-Shipping.exe");
    }();
    return valid;
}
struct DraskPracticeConfiguration {
    bool Enabled = false;
    float Power = 100.0f;
    std::string Error;
};
inline DraskPracticeConfiguration ParseDraskPracticeMarker(const std::string& text) {
    DraskPracticeConfiguration config;
    if (text.empty() || text.size() > 1024) { config.Error = "marker_size_invalid"; return config; }
    std::map<std::string, std::string> fields;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto split = line.find('=');
        if (split == std::string::npos) { config.Error = "marker_requires_key_value_lines"; return config; }
        const auto key = Trim(line.substr(0, split)), value = Trim(line.substr(split + 1));
        if (key != "Enabled" && key != "Power") { config.Error = "unknown_marker_key"; return config; }
        if (!fields.emplace(key, value).second) { config.Error = "duplicate_marker_key"; return config; }
    }
    if (!fields.contains("Enabled") || (fields["Enabled"] != "0" && fields["Enabled"] != "1")) {
        config.Error = "marker_requires_enabled_0_or_1"; return config;
    }
    if (fields.contains("Power")) {
        try {
            size_t consumed = 0;
            config.Power = std::stof(fields["Power"], &consumed);
            if (consumed != fields["Power"].size() || !std::isfinite(config.Power)
                || config.Power < 25.0f || config.Power > 420.0f) {
                config.Error = "power_outside_25_to_420"; return config;
            }
        } catch (...) { config.Error = "invalid_power"; return config; }
    }
    config.Enabled = fields["Enabled"] == "1";
    return config;
}
inline DraskPracticeConfiguration ReadDraskPracticeMarker() {
    const auto path = std::filesystem::path(Root) / L"data/drask-practice.ini";
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return {false, 100.0f, "marker_missing_or_unreadable"};
    if (size == 0 || size > 1024) return {false, 100.0f, "marker_size_invalid"};
    std::ifstream input(path, std::ios::binary);
    if (!input) return {false, 100.0f, "marker_unreadable"};
    // Bound the actual read too, in case a writer changes the file after file_size().
    std::string text(1025, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(input.gcount()));
    if (input.bad()) return {false, 100.0f, "marker_unreadable"};
    return ParseDraskPracticeMarker(text);
}
inline void ApplyDraskPracticePower(UWorld* world, UNetDriver* driver, int port,
    UActorComponent* spawner, AArchonBehemoth* monster, const std::string& requestId) {
    constexpr char monsterPath[] = "/Game/Monsters/drask144/fulgar_alpha_bp.fulgar_alpha_bp_C";
    if (port != 8766 || !IsExpectedProcess() || !world || !driver || driver->World != world
        || world->NetDriver != driver || driver->ServerConnection || !world->AuthorityGameMode
        || !world->AuthorityGameMode->HasAuthority()
        || world->AuthorityGameMode->Class->GetName() != "BPGM_Archon_Prototype_C"
        || ObjectPath(world) != "/Game/Maps/islands/arenas/arena_ramsgate_00.arena_ramsgate_00"
        || !spawner || spawner->GetOwner() != world->AuthorityGameMode
        || !monster || !monster->Class || !monster->HasAuthority() || monster->IsActorBeingDestroyed()
        || ObjectPath(monster->Class) != monsterPath
        || (static_cast<uint32_t>(monster->Flags) & (static_cast<uint32_t>(EObjectFlags::ClassDefaultObject)
            | static_cast<uint32_t>(EObjectFlags::ArchetypeObject)))) return;
    const auto config = ReadDraskPracticeMarker();
    if (!config.Enabled) {
        Log("\"event\":\"drask_practice_power\",\"requestId\":" + Json(requestId)
            + ",\"result\":\"unchanged\",\"reason\":" + Json(config.Error.empty() ? "disabled" : config.Error));
        return;
    }
    using PowerParams = Params::bp_gamemode_behemoth_spawner_component_C_SetBehemothPowerOverride;
    static_assert(offsetof(PowerParams, Behemoth) == 0 && offsetof(PowerParams, Power) == 8
        && sizeof(PowerParams) == 0x70);
    auto* function = FindFunction(spawner, "SetBehemothPowerOverride");
    auto* first = function ? static_cast<FProperty*>(function->ChildProperties) : nullptr;
    auto* second = first ? static_cast<FProperty*>(first->Next) : nullptr;
    if (!function || !function->Outer || function->Outer->GetName() != "bp_gamemode_behemoth_spawner_component_C"
        || !first || !second || !first->ClassPrivate || !second->ClassPrivate
        || first->Name.ToString() != "Behemoth" || first->ClassPrivate->Name.ToString() != "ObjectProperty"
        || first->Offset != 0 || first->ElementSize != 8 || first->ArrayDim != 1
        || second->Name.ToString() != "Power" || second->ClassPrivate->Name.ToString() != "FloatProperty"
        || second->Offset != 8 || second->ElementSize != 4 || second->ArrayDim != 1) {
        Log("\"event\":\"drask_practice_power\",\"requestId\":" + Json(requestId)
            + ",\"result\":\"unchanged\",\"reason\":\"stock_power_function_layout_mismatch\"");
        return;
    }
    const float beforePower = monster->GlobalPower, beforeResistance = monster->GlobalResistance;
    PowerParams params{};
    params.Behemoth = monster;
    params.Power = config.Power;
    // Stock instance-only initialization: GlobalPower/Resistance, UpdateBasePowers/Resists.
    spawner->ProcessEvent(function, &params);
    const bool readback = std::isfinite(monster->GlobalPower) && std::isfinite(monster->GlobalResistance)
        && std::fabs(monster->GlobalPower - config.Power) < 0.001f
        && std::fabs(monster->GlobalResistance - config.Power) < 0.001f;
    Log("\"event\":\"drask_practice_power\",\"requestId\":" + Json(requestId)
        + ",\"serverPid\":" + std::to_string(GetCurrentProcessId()) + ",\"port\":8766"
        + ",\"monsterIndex\":" + std::to_string(monster->Index) + ",\"classPath\":" + Json(monsterPath)
        + ",\"result\":" + Json(readback ? "applied" : "readback_mismatch")
        + ",\"requestedPower\":" + std::to_string(config.Power)
        + ",\"powerBefore\":" + std::to_string(beforePower) + ",\"powerAfter\":" + std::to_string(monster->GlobalPower)
        + ",\"resistanceBefore\":" + std::to_string(beforeResistance) + ",\"resistanceAfter\":" + std::to_string(monster->GlobalResistance)
        + ",\"readback\":" + Bool(readback));
}
inline bool AllowedMonster(const std::string& name) {
    return name == DefaultMonster
        || name == "/Game/Monsters/fulgar/fulgar_normal_bp.fulgar_normal_bp_C"
        || name == "/Game/Monsters/fulgar/fulgar_alpha_bp.fulgar_alpha_bp_C"
        || name == "/Game/Monsters/drask144/fulgar_alpha_bp.fulgar_alpha_bp_C"
        || name == "/Game/Monsters/mcrollin/mcbeaver_frost_bp.mcbeaver_frost_bp_C"
        || name == "/Game/Monsters/frost144/mcbeaver_frost_bp.mcbeaver_frost_bp_C";
}
inline bool ValidId(const std::string& value) {
    if (value.empty() || value.size() > 80) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
    });
}
inline bool ReadRequest(SpawnRequest& request, std::string& error) {
    const auto file = std::filesystem::path(Root) / L"data/spawn.request";
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return false;
    if (std::filesystem::file_size(file, ec) > 4096 || ec) { error = "request_file_too_large_or_unreadable"; return false; }
    std::ifstream input(file, std::ios::binary);
    if (!input) { error = "request_file_unreadable"; return false; }
    std::map<std::string, std::string> entries;
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) { error = "request_requires_key_value_lines"; return false; }
        const auto key = Trim(line.substr(0, equals)), value = Trim(line.substr(equals + 1));
        if (key != "RequestId" && key != "MonsterClass" && key != "SpawnDistance") { error = "unknown_request_key"; return false; }
        if (!entries.emplace(key, value).second) { error = "duplicate_request_key"; return false; }
    }
    request.Id = entries["RequestId"];
    if (!ValidId(request.Id)) { error = "invalid_or_missing_request_id"; return false; }
    if (entries.contains("MonsterClass")) request.MonsterClass = entries["MonsterClass"];
    if (!AllowedMonster(request.MonsterClass)) { error = "monster_class_not_whitelisted"; return false; }
    if (entries.contains("SpawnDistance")) {
        try {
            size_t consumed = 0;
            request.Distance = std::stof(entries["SpawnDistance"], &consumed);
            if (consumed != entries["SpawnDistance"].size() || !std::isfinite(request.Distance)
                || request.Distance < 300.0f || request.Distance > 2500.0f) { error = "spawn_distance_outside_300_to_2500"; return false; }
        } catch (...) { error = "invalid_spawn_distance"; return false; }
    }
    return true;
}
inline bool ExistingActor(const TrackedMonster& tracked) {
    return tracked.Actor && tracked.Index >= 0 && UObject::GObjects->GetByIndex(tracked.Index) == tracked.Actor
        && !(static_cast<uint32_t>(tracked.Actor->Flags) & (static_cast<uint32_t>(EObjectFlags::BeginDestroyed) | static_cast<uint32_t>(EObjectFlags::FinishDestroyed)))
        && tracked.Actor->HasAuthority() && tracked.Actor->GetMaxHealth() > 0 && !tracked.Actor->IsActorBeingDestroyed();
}
inline int GameStarted(UWorld* world) {
    auto* function = FindFunction(world->GameState, "HasGameStarted");
    if (!function) return -1;
    struct { bool Started = false; } params;
    world->GameState->ProcessEvent(function, &params);
    return params.Started ? 1 : 0;
}
inline AActor* ActorOutput(UObject* object, const std::string& name) {
    auto* function = FindFunction(object, name);
    if (!function) return nullptr;
    struct { AActor* Actor = nullptr; } params;
    object->ProcessEvent(function, &params);
    return params.Actor;
}
inline void NavigationDiagnostics(UWorld* world) {
    auto* base = world->NavigationSystem;
    auto* navClass = UObject::FindClassFast("NavigationSystemV1");
    auto* nav = base && navClass && base->IsA(navClass) ? static_cast<UNavigationSystemV1*>(base) : nullptr;
    Log("\"event\":\"navigation_system\",\"class\":" + (base ? Json(base->Class->GetName()) : "null")
        + ",\"isNavigationSystemV1\":" + Bool(nav != nullptr)
        + ",\"mainNavData\":" + (nav && nav->MainNavData ? Json(nav->MainNavData->GetName()) : "null")
        + ",\"navDataCount\":" + std::to_string(nav ? nav->NavDataSet.Num() : 0));
    if (!nav || nav->NavDataSet.Num() < 0 || nav->NavDataSet.Num() > 64) return;
    for (auto* data : nav->NavDataSet) {
        if (!data) continue;
        Log("\"event\":\"navigation_data\",\"name\":" + Json(data->GetName())
            + ",\"class\":" + Json(data->Class->GetName())
            + ",\"agentRadius\":" + std::to_string(data->NavDataConfig.AgentRadius)
            + ",\"agentHeight\":" + std::to_string(data->NavDataConfig.AgentHeight));
    }
}
inline void Sample(UWorld* world, APawn* player) {
    const int started = GameStarted(world);
    std::string monsterList = "[";
    bool first = true;
    for (auto& tracked : Monsters) {
        if (!ExistingActor(tracked)) continue;
        auto* monster = tracked.Actor;
        const auto position = monster->K2_GetActorLocation(), velocity = monster->GetVelocity();
        const auto health = monster->GetCurrentHealth();
        auto* target = ActorOutput(monster, "GetCurrentTarget_interface");
        auto* patrol = ActorOutput(monster, "GetCurrentPatrolPoint");
        auto* animation = monster->Mesh ? monster->Mesh->GetAnimInstance() : nullptr;
        auto* montage = animation ? animation->GetCurrentActiveMontage() : nullptr;
        std::string aggroFields = ",\"aggroTargets\":null";
        auto* aggroClass = UObject::FindClassFast("ArchonAggroComponent");
        auto* aggro = aggroClass ? static_cast<UArchonAggroComponent*>(monster->GetComponentByClass(aggroClass)) : nullptr;
        if (aggro) {
            aggroFields = ",\"aggroTargets\":" + std::to_string(aggro->NumValidTargets)
                + ",\"aggroActive\":" + Bool(aggro->IsActive())
                + ",\"currentTargetIndex\":" + std::to_string(aggro->CurrentTargetIndex)
                + ",\"hasBlackboard\":" + Bool(aggro->CachedBlackboard != nullptr)
                + ",\"hasTether\":" + Bool(aggro->GetTetherActor() != nullptr);
            if (aggro->TargetData.Num() >= 0 && aggro->TargetData.Num() <= 128 && aggro->TargetData.IsValidIndex(aggro->CurrentTargetIndex)) {
                const auto& data = aggro->TargetData[aggro->CurrentTargetIndex];
                aggroFields += ",\"targetPathExists\":" + std::string(Bool(data.PathExists))
                    + ",\"targetAccessibility\":" + std::to_string(static_cast<int>(data.Accessibility))
                    + ",\"targetDeaggroState\":" + std::to_string(static_cast<int>(data.DeaggroState));
            }
        }
        const auto body = "\"requestId\":" + Json(tracked.RequestId) + ",\"monsterIndex\":" + std::to_string(tracked.Index)
            + ",\"class\":" + Json(monster->Class->GetName())
            + ",\"classPath\":" + Json(ObjectPath(monster->Class))
            + ",\"activeMontage\":" + (montage ? Json(ObjectPath(montage)) : "null")
            + ",\"spawnMethod\":" + Json(tracked.SpawnMethod) + ",\"spawnPoint\":" + Json(tracked.SpawnPoint)
            + ",\"spawnX\":" + std::to_string(tracked.SpawnLocation.X) + ",\"spawnY\":" + std::to_string(tracked.SpawnLocation.Y)
            + ",\"spawnZ\":" + std::to_string(tracked.SpawnLocation.Z) + ",\"ownAgentProjected\":" + Bool(tracked.OwnAgentProjected)
            + ",\"health\":" + std::to_string(health) + ",\"maxHealth\":" + std::to_string(monster->GetMaxHealth())
            + ",\"x\":" + std::to_string(position.X) + ",\"y\":" + std::to_string(position.Y) + ",\"z\":" + std::to_string(position.Z)
            + ",\"speed\":" + std::to_string(std::sqrt(velocity.X*velocity.X + velocity.Y*velocity.Y + velocity.Z*velocity.Z))
            + ",\"controller\":" + Bool(monster->Controller != nullptr) + ",\"authority\":" + Bool(monster->HasAuthority())
            + ",\"currentTarget\":" + (target ? Json(target->GetName()) : "null")
            + ",\"targetIsPlayer\":" + Bool(player && target == player)
            + ",\"patrolPoint\":" + (patrol ? Json(patrol->GetName()) : "null") + aggroFields;
        Log("\"event\":\"sample\",\"gameStarted\":" + std::to_string(started) + "," + body);
        if (!first) monsterList += ',';
        first = false;
        monsterList += '{' + body + '}';
        if (health <= 0 && !tracked.DeathReported) { tracked.DeathReported = true; Log("\"event\":\"monster_dead\",\"requestId\":" + Json(tracked.RequestId) + ",\"monsterIndex\":" + std::to_string(tracked.Index)); }
    }
    monsterList += ']';
    std::string playerFields;
    if (player) {
        const auto position = player->K2_GetActorLocation();
        float health = -1.0f;
        auto* characterClass = UObject::FindClassFast("ArchonCharacter");
        if (characterClass && player->IsA(characterClass)) health = static_cast<AArchonCharacter*>(player)->GetHealth();
        playerFields = ",\"playerX\":" + std::to_string(position.X) + ",\"playerY\":" + std::to_string(position.Y)
            + ",\"playerZ\":" + std::to_string(position.Z) + ",\"playerHealth\":" + std::to_string(health);
    }
    AtomicJson(L"dev-state.json", "{\"serverPid\":" + std::to_string(GetCurrentProcessId())
        + ",\"map\":" + Json(world->GetName()) + ",\"mode\":" + Json(world->AuthorityGameMode->Class->GetName())
        + ",\"playerReady\":" + Bool(player != nullptr) + ",\"gameStarted\":" + (started < 0 ? "null" : Bool(started == 1))
        + playerFields + ",\"monsters\":" + monsterList + ",\"status\":" + Json(LastStatus)
        + ",\"lastRequestId\":" + Json(LastRequestId) + ",\"lastRequestResult\":" + Json(LastRequestResult)
        + ",\"uptimeMs\":" + std::to_string(GetTickCount64()) + "}");
}
inline void Tick(UWorld* world, UNetDriver* driver, int port) {
    if (port != 8766 || !IsExpectedProcess() || !world || !driver || GetTickCount64() < NextPoll) return;
    NextPoll = GetTickCount64() + 500;
    if (world->NetDriver != driver || driver->ServerConnection) { Status("waiting_for_server_world"); return; }
    auto* mode = world->AuthorityGameMode;
    if (!mode || !mode->HasAuthority()) { Status("waiting_for_authority"); return; }
    if (LastWorld != world) {
        LastWorld = world;
        Monsters.clear();
        Log("\"event\":\"world\",\"mode\":" + Json(mode->Class->GetName()) + ",\"map\":" + Json(world->GetName()));
        NavigationDiagnostics(world);
        for (auto* component : mode->K2_GetComponentsByClass(UActorComponent::StaticClass()))
            if (component) Log("\"event\":\"component\",\"class\":" + Json(component->Class->GetName()));
    }
    APawn* player = nullptr;
    auto* playerClass = UObject::FindClassFast("BP_PlayerCharacter_C");
    for (auto* connection : driver->ClientConnections) {
        auto* candidate = connection && connection->PlayerController ? connection->PlayerController->Pawn : nullptr;
        if (candidate && playerClass && candidate->IsA(playerClass) && !candidate->IsActorBeingDestroyed()) { player = candidate; break; }
    }
    Monsters.erase(std::remove_if(Monsters.begin(), Monsters.end(), [](const auto& tracked) { return !ExistingActor(tracked); }), Monsters.end());
    if (!player) Status("waiting_for_player");
    if (GetTickCount64() >= NextSample) {
        NextSample = GetTickCount64() + (FrostHuntEnabled() ? 500 : 2000);
        if (FrostHuntEnabled()) {
            auto* behemothClass = UObject::FindClassFast("ArchonBehemoth");
            TArray<AActor*> actors;
            if (behemothClass) UGameplayStatics::GetAllActorsOfClass(world, behemothClass, &actors);
            if (actors.Num() >= 0 && actors.Num() < 64) for (auto* actor : actors) {
                if (!actor || !actor->HasAuthority() || actor->IsActorBeingDestroyed()) continue;
                const auto known = std::find_if(Monsters.begin(), Monsters.end(), [&](const auto& tracked) { return tracked.Actor == actor; });
                if (known == Monsters.end()) {
                    auto* behemoth = static_cast<AArchonBehemoth*>(actor);
                    if (behemoth->GetMaxHealth() > 0) Monsters.push_back({behemoth, behemoth->Index,
                        "hunt-" + std::to_string(behemoth->Index), false, "stock_hunt", "", behemoth->K2_GetActorLocation(), true});
                }
            }
        }
        Sample(world, player);
    }
    if (!player) return;
    if (FrostHuntEnabled()) { Status("frost_hunt_automatic_spawning"); return; }
    SpawnRequest request;
    std::string error;
    if (!ReadRequest(request, error)) {
        if (!error.empty()) {
            if (ValidId(request.Id)) ConsumedRequests.insert(request.Id);
            Result(request, "rejected", error);
        }
        Status(error.empty() ? "ready_for_spawn_request" : error);
        return;
    }
    if (ConsumedRequests.contains(request.Id)) { Result(request, "duplicate", "request_already_processed"); return; }
    const auto living = std::count_if(Monsters.begin(), Monsters.end(), [](const auto& tracked) { return ExistingActor(tracked) && tracked.Actor->GetCurrentHealth() > 0; });
    if (living >= 3) { ConsumedRequests.insert(request.Id); Result(request, "rejected", "three_monsters_already_alive"); return; }
    auto* spawnerClass = UObject::FindClassFast("bp_gamemode_behemoth_spawner_component_C");
    auto* spawner = spawnerClass ? mode->GetComponentByClass(spawnerClass) : nullptr;
    auto* spawnFunction = FindFunction(spawner, "SpawnBehemothAtLocation");
    if (!spawner || !spawnFunction) { ConsumedRequests.insert(request.Id); Result(request, "rejected", "map_has_no_standard_behemoth_spawner"); return; }
    const std::wstring classPath(request.MonsterClass.begin(), request.MonsterClass.end());
    const auto softPath = UKismetSystemLibrary::MakeSoftClassPath(classPath.c_str());
    auto* monsterClass = UKismetSystemLibrary::LoadClassAsset_Blocking(UKismetSystemLibrary::Conv_SoftClassPathToSoftClassRef(softPath));
    auto* base = UObject::FindClassFast("ArchonBehemoth");
    bool validClass = false;
    for (UStruct* type = monsterClass; type; type = type->SuperStruct) if (type == base) { validClass = true; break; }
    if (!validClass) { ConsumedRequests.insert(request.Id); Result(request, "failed", "monster_class_load_failed"); return; }
    auto* typedSpawner = static_cast<Ubp_gamemode_behemoth_spawner_component_C*>(spawner);
    typedSpawner->EnableBehemothIntros = false;
    AArchonBehemoth* spawned = nullptr;
    std::string spawnMethod = "location", spawnPoint;
    FVector spawnPosition{};
    ABP_PatrolCenterPoint_C* selectedPatrol = nullptr;
    auto* patrolFunction = FindFunction(spawner, "SpawnBehemothAtPatrolPoint");
    auto* allowedFunction = FindFunction(spawner, "GetAllowedSpawnPoints");
    if (mode->Class->GetName() == "BPGM_Archon_Prototype_C" && patrolFunction && allowedFunction) {
        Params::bp_gamemode_behemoth_spawner_component_C_GetAllowedSpawnPoints points{};
        spawner->ProcessEvent(allowedFunction, &points);
        const auto origin = player->K2_GetActorLocation();
        float bestDistance = (std::numeric_limits<float>::max)();
        if (points.SpawnPoints.Num() >= 0 && points.SpawnPoints.Num() <= 1024) {
            for (auto* point : points.SpawnPoints) {
                if (!point || point->IsActorBeingDestroyed() || !point->AllowedSpawnLocation) continue;
                const auto position = point->K2_GetActorLocation();
                const auto distance = (position.X-origin.X)*(position.X-origin.X)
                    + (position.Y-origin.Y)*(position.Y-origin.Y) + (position.Z-origin.Z)*(position.Z-origin.Z);
                if (std::isfinite(distance) && distance < bestDistance) { selectedPatrol = point; bestDistance = distance; }
            }
        }
        Log("\"event\":\"patrol_selection\",\"requestId\":" + Json(request.Id)
            + ",\"allowedPointCount\":" + std::to_string(points.SpawnPoints.Num())
            + ",\"point\":" + (selectedPatrol ? Json(selectedPatrol->GetName()) : "null"));
    }
    if (selectedPatrol) {
        Params::bp_gamemode_behemoth_spawner_component_C_SpawnBehemothAtPatrolPoint params{};
        params.MonsterClass = monsterClass;
        params.PatrolPoint = selectedPatrol;
        params.CinematicMarker = nullptr;
        params.SpawnParams.DelayAfterDeathBeforeDissolve_12_BF65FBEA4583569FF6942D9D01DC13DD = 10.0f;
        spawnMethod = "patrol";
        spawnPoint = selectedPatrol->GetName();
        spawnPosition = selectedPatrol->K2_GetActorLocation();
        ConsumedRequests.insert(request.Id);
        spawner->ProcessEvent(patrolFunction, &params);
        spawned = params.Behemoth;
    } else {
    const auto playerPosition = player->K2_GetActorLocation(), forward = player->GetActorForwardVector();
    const FVector desired{playerPosition.X + forward.X * request.Distance, playerPosition.Y + forward.Y * request.Distance, playerPosition.Z};
    TArray<AActor*> ignore;
    ignore.Add(player);
    for (const auto& tracked : Monsters) if (ExistingActor(tracked)) ignore.Add(tracked.Actor);
    FHitResult hit{};
    const bool grounded = UKismetSystemLibrary::LineTraceSingle(world,
        FVector{desired.X, desired.Y, desired.Z + 600.0f}, FVector{desired.X, desired.Y, desired.Z - 2500.0f},
        ETraceTypeQuery::TraceTypeQuery1, false, ignore, EDrawDebugTrace::None, &hit, true, {}, {}, 0.0f)
        && hit.bBlockingHit && !hit.bStartPenetrating && hit.ImpactNormal.Z >= 0.55f;
    if (!grounded) { ConsumedRequests.insert(request.Id); Result(request, "rejected", "no_safe_ground_ahead_of_player"); return; }
    const FVector ground{hit.ImpactPoint.X, hit.ImpactPoint.Y, hit.ImpactPoint.Z};
    FVector projected{};
    const bool nav = UNavigationSystemV1::K2_ProjectPointToNavigation(world, ground, &projected, nullptr, {}, FVector{350,350,600});
    Log("\"event\":\"spawn_plan\",\"requestId\":" + Json(request.Id) + ",\"navigationProjected\":" + Bool(nav)
        + ",\"grounded\":true,\"gameStarted\":" + std::to_string(GameStarted(world))
        + ",\"groundX\":" + std::to_string(ground.X) + ",\"groundY\":" + std::to_string(ground.Y) + ",\"groundZ\":" + std::to_string(ground.Z));
    Params::bp_gamemode_behemoth_spawner_component_C_SpawnBehemothAtLocation params{};
    params.MonsterClass = monsterClass;
    params.Location = nav ? projected : ground;
    if (!nav) params.Location.Z += 120.0f;
    params.Rotation = player->K2_GetActorRotation();
    params.Rotation.Yaw += 180.0f;
    params.ZonePreset = 0;
    params.SpawnParams.DelayAfterDeathBeforeDissolve_12_BF65FBEA4583569FF6942D9D01DC13DD = 10.0f;
    params.TryToAlignToGround = nav;
    // Consume before invoking game code so callbacks cannot duplicate the request.
    ConsumedRequests.insert(request.Id);
    spawner->ProcessEvent(spawnFunction, &params);
    spawned = params.Behemoth;
    spawnPosition = params.Location;
    }
    if (!spawned) { Result(request, "failed", "spawner_returned_no_actor"); return; }
    ApplyDraskPracticePower(world, driver, port, spawner, spawned, request.Id);
    const auto actualPosition = spawned->K2_GetActorLocation();
    FVector agentProjected{};
    bool ownAgentProjected = false;
    UArchonGameplayStatics::ProjectPointToNavigationForActor(spawned, actualPosition, &agentProjected, &ownAgentProjected, {}, FVector{600,600,1200});
    Monsters.push_back({spawned, spawned->Index, request.Id, false, spawnMethod, spawnPoint, spawnPosition, ownAgentProjected});
    spawned->ForceNetUpdate();
    Result(request, "spawned");
    Log("\"event\":\"spawned\",\"requestId\":" + Json(request.Id) + ",\"monsterIndex\":" + std::to_string(spawned->Index)
        + ",\"class\":" + Json(spawned->Class->GetName()) + ",\"authority\":" + Bool(spawned->HasAuthority())
        + ",\"spawnMethod\":" + Json(spawnMethod) + ",\"point\":" + Json(spawnPoint)
        + ",\"x\":" + std::to_string(actualPosition.X) + ",\"y\":" + std::to_string(actualPosition.Y) + ",\"z\":" + std::to_string(actualPosition.Z)
        + ",\"ownAgentProjected\":" + Bool(ownAgentProjected));
    Status("monster_spawned");
}
}
