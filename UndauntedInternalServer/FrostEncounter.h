// Isolated Dojo144 encounter registration and stock HUD recovery. AGPL-3.0-only.
#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include "DojoProbe.h"
#include "FrostHuntConfig.h"
#include "FrostIcon.h"
#include "HuntWorld.h"
#include "SDK/BPGS_Archon_GameState_classes.hpp"
#include "SDK/UMG_classes.hpp"
#include "SDK/UMG_parameters.hpp"

namespace FrostEncounter {
using namespace SDK;
inline UWorld* ServerWorld = nullptr;
inline UWorld* ClientWorld = nullptr;
inline ULONGLONG NextServerPoll = 0, NextClientPoll = 0;
inline std::map<std::string, int> RegistrationAttempts;
inline std::set<std::string> ReportedSpawns, ReportedHud;

inline void Log(const std::string& body) {
    std::ofstream out(std::filesystem::path(DojoProbe::Root()) / L"reports/frost-encounter.jsonl", std::ios::app);
    out << "{\"pid\":" << GetCurrentProcessId() << ",\"uptimeMs\":" << GetTickCount64() << ',' << body << "}\n";
}
inline bool IsFrost(AArchonBehemoth* behemoth) {
    if (!behemoth || behemoth->IsActorBeingDestroyed() || !behemoth->Class) return false;
    auto* package = behemoth->Class->Outer;
    while (package && package->Outer) package = package->Outer;
    const auto path = package ? package->Name.GetRawString() : std::string{};
    return behemoth->Class->GetName() == "mcbeaver_frost_bp_C"
        && (path == "/Game/Monsters/mcrollin/mcbeaver_frost_bp" || path == "/Game/Monsters/frost144/mcbeaver_frost_bp");
}
using HuntWorld::Contains;
using HuntWorld::State;
using HuntWorld::IslandMode;
inline void WorldDiagnostics(UWorld* world, const char* side) {
    auto* state = State(world);
    auto* feature = UObject::FindClassFast("ReturnToRamsgateFeature");
    const bool returnEnabled = feature && UArchonFeatureFlagBlueprintLibrary::IsFeatureEnabled(world, feature);
    Log("\"event\":\"world\",\"side\":" + DojoProbe::Json(side)
        + ",\"map\":" + DojoProbe::Json(world->GetName())
        + ",\"gameStateClass\":" + (world->GameState ? DojoProbe::Json(world->GameState->Class->GetName()) : "null")
        + ",\"matchmakerHuntId\":" + (state ? DojoProbe::Json(state->MatchmakerHuntID.ToString()) : "null")
        + ",\"gameStarted\":" + (state ? DojoProbe::Bool(state->GameStarted) : "null")
        + ",\"isTrainingGround\":" + DojoProbe::Bool(UArchonGameplayStatics::IsInTrainingGround(world))
        + ",\"returnFeatureAvailable\":" + DojoProbe::Bool(feature != nullptr)
        + ",\"returnFeatureEnabled\":" + DojoProbe::Bool(returnEnabled));
}
inline void SpawnDiagnostics(UWorld* world, AArchonBehemoth* behemoth, const std::string& key) {
    if (!ReportedSpawns.insert(key).second) return;
    struct { FName Name; } metadata{};
    if (auto* function = DojoProbe::FindFunction(behemoth, "GetMetadataTableName")) behemoth->ProcessEvent(function, &metadata);
    struct { Fbehemoth_spawn_params Params; } spawn{};
    const bool hasSpawnParams = DojoProbe::FindFunction(behemoth, "GetSpawnParams") != nullptr;
    if (hasSpawnParams) behemoth->ProcessEvent(DojoProbe::FindFunction(behemoth, "GetSpawnParams"), &spawn);
    FString archetype, classification;
    behemoth->GetBehemothNameDataNative(&archetype, &classification);
    auto* mode = IslandMode(world);
    auto* state = State(world);
    Log("\"event\":\"spawn_diagnostics\",\"monsterIndex\":" + std::to_string(behemoth->Index)
        + ",\"class\":" + DojoProbe::Json(behemoth->Class->GetFullName())
        + ",\"metadataKey\":" + DojoProbe::Json(metadata.Name.ToString())
        + ",\"archetype\":" + DojoProbe::Json(archetype.ToString()) + ",\"classification\":" + DojoProbe::Json(classification.ToString())
        + ",\"matchmakerHuntId\":" + (state ? DojoProbe::Json(state->MatchmakerHuntID.ToString()) : "null")
        + ",\"modeTracked\":" + DojoProbe::Bool(mode && Contains(mode->SpawnedBehemoths, behemoth))
        + ",\"stateTracked\":" + DojoProbe::Bool(state && Contains(state->SpawnedBehemoths, behemoth))
        + ",\"imageRevealed\":" + DojoProbe::Bool(behemoth->bImageRevealed)
        + ",\"hasSpawnParams\":" + DojoProbe::Bool(hasSpawnParams)
        + ",\"triggersEndOfMission\":" + DojoProbe::Bool(spawn.Params.TriggersEndOfMission_1_D5DFABCB49AC889D090A90B4FFC5588B)
        + ",\"temporarySpawn\":" + DojoProbe::Bool(spawn.Params.IsTemporarySpawnedBehemoth_5_22372EA1469479FC8833F3A5F783A857)
        + ",\"maxHealth\":" + std::to_string(behemoth->GetMaxHealth()));
}
// Call after a manual spawn, or let TickServer discover the normal hunt's spawn.
// Only missing stock registrations are repaired; existing death/reward bindings are left alone.
inline bool RegisterSpawn(UWorld* world, AArchonBehemoth* behemoth, int port) {
    if ((port != 8766 && !FrostHuntConfig::IsDedicatedServer(world, port)) || !DojoProbe::IsExpectedProcess() || !world || !world->NetDriver || world->NetDriver->ServerConnection
        || !world->AuthorityGameMode || !world->AuthorityGameMode->HasAuthority() || !IsFrost(behemoth)
        || !behemoth->HasAuthority() || !behemoth->Controller || behemoth->GetCurrentHealth() <= 0) return false;
    auto* mode = IslandMode(world);
    auto* state = State(world);
    if (!mode || !state || world->AuthorityGameMode->Class->GetName() != "BPGM_Archon_Prototype_C") return false;
    const auto key = std::to_string(behemoth->Index) + ":" + behemoth->GetFullName();
    // Normal hunt spawning already owns kill, part and reward bindings.
    if (FrostHuntConfig::IsDedicatedWorld(world) || DojoProbe::FrostHuntEnabled()) {
        SpawnDiagnostics(world, behemoth, key);
        return Contains(mode->SpawnedBehemoths, behemoth) && Contains(state->SpawnedBehemoths, behemoth);
    }
    if (Contains(mode->SpawnedBehemoths, behemoth) && Contains(state->SpawnedBehemoths, behemoth)) {
        SpawnDiagnostics(world, behemoth, key);
        return true;
    }
    if (RegistrationAttempts[key] >= 3) return false;
    ++RegistrationAttempts[key];
    const bool modeBefore = Contains(mode->SpawnedBehemoths, behemoth), stateBefore = Contains(state->SpawnedBehemoths, behemoth);
    bool calledMode = false, calledState = false;
    struct { AArchonBehemoth* Behemoth; } params{behemoth};
    if (!modeBefore) if (auto* function = DojoProbe::FindFunction(mode, "RegisterBehemothSpawned")) {
        mode->ProcessEvent(function, &params);
        calledMode = true;
    }
    // The normal game mode registration normally reaches this itself.
    if (!Contains(state->SpawnedBehemoths, behemoth)) if (auto* function = DojoProbe::FindFunction(state, "TrackBehemothSpawn")) {
        state->ProcessEvent(function, &params);
        calledState = true;
    }
    const bool modeAfter = Contains(mode->SpawnedBehemoths, behemoth), stateAfter = Contains(state->SpawnedBehemoths, behemoth);
    state->ForceNetUpdate();
    behemoth->ForceNetUpdate();
    Log("\"event\":\"registration\",\"monsterIndex\":" + std::to_string(behemoth->Index)
        + ",\"modeBefore\":" + DojoProbe::Bool(modeBefore) + ",\"stateBefore\":" + DojoProbe::Bool(stateBefore)
        + ",\"calledModeRegistration\":" + DojoProbe::Bool(calledMode) + ",\"calledStateTracking\":" + DojoProbe::Bool(calledState)
        + ",\"modeAfter\":" + DojoProbe::Bool(modeAfter) + ",\"stateAfter\":" + DojoProbe::Bool(stateAfter));
    SpawnDiagnostics(world, behemoth, key);
    return modeAfter && stateAfter;
}
inline void TickServer(UWorld* world, UNetDriver* driver, int port) {
    if ((port != 8766 && !FrostHuntConfig::IsDedicatedServer(world, port)) || !DojoProbe::IsExpectedProcess() || !world || !driver || world->NetDriver != driver || driver->ServerConnection
        || !world->AuthorityGameMode || !world->AuthorityGameMode->HasAuthority() || GetTickCount64() < NextServerPoll) return;
    NextServerPoll = GetTickCount64() + 1000;
    if (ServerWorld != world) {
        ServerWorld = world;
        RegistrationAttempts.clear(); ReportedSpawns.clear();
        WorldDiagnostics(world, "server");
    }
    TArray<AActor*> actors;
    UGameplayStatics::GetAllActorsOfClass(world, AArchonBehemoth::StaticClass(), &actors);
    if (actors.Num() < 0 || actors.Num() > 128) return;
    for (auto* actor : actors) if (actor) RegisterSpawn(world, static_cast<AArchonBehemoth*>(actor), port);
}
inline bool SlotHas(UHUDBehemothStateWidget* slot, AArchonBehemoth* behemoth) {
    return slot && slot->Behemoth.Get() == behemoth;
}
// Optional client recovery, called after the client engine tick (never from inside ProcessEvent).
// It replays the stock HUD notification only when all three existing slots missed the spawn.
inline void TickClient(UWorld* world) {
    if (!DojoProbe::IsExpectedProcess() || !world || !world->NetDriver || !world->NetDriver->ServerConnection
        || world->AuthorityGameMode || GetTickCount64() < NextClientPoll) return;
    NextClientPoll = GetTickCount64() + 1000;
    const auto map = world->GetName();
    if (map.find("arena_ramsgate_00") == std::string::npos && map.find("dia_moss_triforce") == std::string::npos
        && map.find("training_dojo_persistent") == std::string::npos && !FrostHuntConfig::IsDedicatedWorld(world)) return;
    auto* player = UArchonGameplayStatics::GetArchonLocalPlayerController(world);
    if (!player || !player->Pawn) return;
    if (ClientWorld != world) {
        ClientWorld = world; ReportedHud.clear();
        WorldDiagnostics(world, "client");
        auto* huntType = UObject::FindClassFast("HuntSystemComponent");
        auto* hunt = huntType ? static_cast<UHuntSystemComponent*>(player->GetComponentByClass(huntType)) : nullptr;
        Log("\"event\":\"client_travel_state\",\"playerHuntId\":" + (hunt ? DojoProbe::Json(hunt->GetPlayerHuntID().ToString()) : "null")
            + ",\"fastTravelPending\":" + DojoProbe::Bool(player->bFastTravelToCityPending));
    }
    TArray<AActor*> actors;
    UGameplayStatics::GetAllActorsOfClass(world, AArchonBehemoth::StaticClass(), &actors);
    if (actors.Num() < 0 || actors.Num() > 128) return;
    Params::WidgetBlueprintLibrary_GetAllWidgetsOfClass query{};
    query.WorldContextObject = world;
    query.WidgetClass = UHUDBehemothHealthManagerWidget::StaticClass();
    query.TopLevelOnly = false;
    auto* libraryType = UObject::FindClassFast("WidgetBlueprintLibrary");
    auto* library = libraryType ? libraryType->ClassDefaultObject : nullptr;
    auto* queryFunction = DojoProbe::FindFunction(library, "GetAllWidgetsOfClass");
    if (!library || !queryFunction) return;
    library->ProcessEvent(queryFunction, &query);
    const auto& widgets = query.FoundWidgets;
    if (widgets.Num() < 0 || widgets.Num() > 32) return;
    for (auto* actor : actors) {
        auto* behemoth = static_cast<AArchonBehemoth*>(actor);
        if (!IsFrost(behemoth) || behemoth->GetCurrentHealth() <= 0) continue;
        for (auto* widget : widgets) {
            if (!widget || DojoProbe::ActorOutput(widget, "GetOwningPlayer") != player) continue;
            auto* manager = static_cast<UHUDBehemothHealthManagerWidget*>(widget);
            if (!manager->BehemothStateWidget1 && !manager->BehemothStateWidget2 && !manager->BehemothStateWidget3) continue;
            const bool bound = SlotHas(manager->BehemothStateWidget1, behemoth) || SlotHas(manager->BehemothStateWidget2, behemoth)
                || SlotHas(manager->BehemothStateWidget3, behemoth);
            FrostIcon::Apply(manager->BehemothStateWidget1);
            FrostIcon::Apply(manager->BehemothStateWidget2);
            FrostIcon::Apply(manager->BehemothStateWidget3);
            const auto key = std::to_string(manager->Index) + ":" + std::to_string(behemoth->Index);
            if (!ReportedHud.insert(key).second) continue;
            if (!bound) manager->OnBehemothSpawned(behemoth);
            UHUDBehemothStateWidget* slot = SlotHas(manager->BehemothStateWidget1, behemoth) ? manager->BehemothStateWidget1
                : SlotHas(manager->BehemothStateWidget2, behemoth) ? manager->BehemothStateWidget2
                : SlotHas(manager->BehemothStateWidget3, behemoth) ? manager->BehemothStateWidget3 : nullptr;
            auto* image = slot ? slot->BehemothIcon : nullptr;
            FrostIcon::Apply(slot);
            Log("\"event\":\"hud_registration\",\"monsterIndex\":" + std::to_string(behemoth->Index)
                + ",\"alreadyBound\":" + DojoProbe::Bool(bound) + ",\"boundAfterNotification\":" + DojoProbe::Bool(slot != nullptr)
                + ",\"imageRevealed\":" + DojoProbe::Bool(behemoth->bImageRevealed)
                + ",\"iconResource\":" + (image && image->Brush.ResourceObject ? DojoProbe::Json(image->Brush.ResourceObject->GetFullName()) : "null"));
        }
    }
}
}
