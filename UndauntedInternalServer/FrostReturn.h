// Enable the native Ramsgate return feature before the isolated client's menu is built. AGPL-3.0-only.
#pragma once
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "DojoProbe.h"
#include "FrostHuntConfig.h"
#include "ThunderdeepHuntConfig.h"
#include "TestArenaPolicy.h"
#include "SDK/UMG_classes.hpp"
#include "SDK/UMG_parameters.hpp"
#include "MinHook/MinHook.h"

namespace FrostReturn {
using namespace SDK;
struct BranchSetting { int32 Index; EArchonFeatureBranches Key; bool Value; };
struct EnvironmentSetting { int32 Index; std::string Key; bool Value; };
inline UArchonFeature* Feature = nullptr;
inline int32 FeatureIndex = -1;
inline bool Active = false, OriginalEnabled = false;
inline ULONGLONG NextPoll = 0;
inline std::vector<BranchSetting> OriginalBranches;
inline std::vector<EnvironmentSetting> OriginalEnvironments;
inline UWorld* FailedWorld = nullptr;
inline UWorld* MenuWorld = nullptr;
inline std::string MenuMap;
inline int LastMenuScreenCount = -1;
inline ULONGLONG NextMenuPoll = 0;
inline std::map<int32, std::string> MenuSnapshots;
// The audited native virtual initializer returns success in AL. Its second
// argument is forwarded unchanged; the hook never reads player/account data.
using ReturnInitializeFn = bool(__fastcall*)(UMainMenuReturnToRamsgateItemViewModel*, AArchonPlayerController*);
inline ReturnInitializeFn OriginalReturnInitialize = nullptr;
using ReturnConfirmedFn = void(__fastcall*)(UMainMenuReturnToRamsgateItemViewModel*);
inline ReturnConfirmedFn OriginalReturnConfirmed = nullptr;
using CancelMatchmakingFn = void(__fastcall*)(AArchonPartyClient*);
inline CancelMatchmakingFn OriginalCancelMatchmaking = nullptr;
inline bool ReturnHookInstalled = false;
struct ReturnBinding {
    UMainMenuReturnToRamsgateItemViewModel* Item;
    AArchonPlayerController* Player;
    int32 PlayerIndex;
    UWorld* World;
};
inline std::map<int32, ReturnBinding> ReturnBindings;
struct ConfirmedTravel {
    UWorld* World = nullptr;
    AArchonPlayerController* Player = nullptr;
    UArchonOnlineSessionClient* Session = nullptr;
    AArchonPartyClient* Party = nullptr;
    int32 PlayerIndex = -1, SessionIndex = -1, PartyIndex = -1;
    ULONGLONG Id = 0, Deadline = 0, NextPoll = 0;
    bool SawIncomplete = false, ReportedCached = false;
};
inline ConfirmedTravel PendingTravel;
inline ULONGLONG NextConfirmationId = 0;

inline void Log(const std::string& body) {
    std::ofstream out(std::filesystem::path(DojoProbe::Root) / L"reports/frost-return.jsonl", std::ios::app);
    out << "{\"pid\":" << GetCurrentProcessId() << ",\"uptimeMs\":" << GetTickCount64() << ',' << body << "}\n";
}
inline bool ValidFeature() {
    return Feature && FeatureIndex >= 0 && UObject::GObjects->GetByIndex(FeatureIndex) == Feature;
}
inline void Restore() {
    if (!Active) return;
    const bool valid = ValidFeature();
    if (valid) {
        Feature->bEnabled = OriginalEnabled;
        for (const auto& setting : OriginalBranches)
            if (Feature->PerBranchSettings.IsValidIndex(setting.Index) && Feature->PerBranchSettings[setting.Index].Key() == setting.Key)
                Feature->PerBranchSettings[setting.Index].Value() = setting.Value;
        for (const auto& setting : OriginalEnvironments)
            if (Feature->PerEnvironmentSettings.IsValidIndex(setting.Index)
                && Feature->PerEnvironmentSettings[setting.Index].Key().ToString() == setting.Key)
                Feature->PerEnvironmentSettings[setting.Index].Value() = setting.Value;
    }
    Log("\"event\":\"native_return_feature_restored\",\"featureValid\":" + std::string(DojoProbe::Bool(valid)));
    Active = false; Feature = nullptr; FeatureIndex = -1;
    OriginalBranches.clear(); OriginalEnvironments.clear();
}
inline bool IsFrostArenaClient(UWorld* world) {
    if (!DojoProbe::IsExpectedProcess() || !world || world->AuthorityGameMode
        || !world->NetDriver || !world->NetDriver->ServerConnection || !world->GameState) return false;
    if (FrostHuntConfig::IsDedicatedWorld(world) || ThunderdeepHuntConfig::IsDedicatedWorld(world)) return true;
    const auto map = world->GetName();
    if (map.find("arena_ramsgate_00") == std::string::npos && map.find("dia_moss_triforce") == std::string::npos
        && map.find("training_dojo_persistent") == std::string::npos) return false;
    auto* stateType = UObject::FindClassFast("ArchonGameState");
    if (!stateType || !world->GameState->IsA(stateType)) return false;
    const auto hunt = static_cast<AArchonGameState*>(world->GameState)->MatchmakerHuntID.ToString();
    if (TestArenaPolicy::IsArena(DojoProbe::ObjectPath(world), hunt,
        DojoProbe::ObjectPath(world->GameState->GameModeClass.Get()))) return true;
    return hunt.empty() || hunt == "None" || hunt == "CR19_MatchmakerHunt_Beaver" || hunt == "ShatteredIsles_TrainingDojo";
}
inline UWorld* ControllerWorld(AArchonPlayerController* player) {
    auto* levelType = UObject::FindClassFast("Level");
    auto* outer = player ? player->Outer : nullptr;
    return outer && levelType && outer->IsA(levelType) ? static_cast<ULevel*>(outer)->OwningWorld : nullptr;
}
inline bool IsLabReturnWorld(UWorld* world) {
    if (!IsFrostArenaClient(world)) return false;
    const auto hunt = static_cast<AArchonGameState*>(world->GameState)->MatchmakerHuntID.ToString();
    if (TestArenaPolicy::IsArena(DojoProbe::ObjectPath(world), hunt,
        DojoProbe::ObjectPath(world->GameState->GameModeClass.Get()))) return true;
    if (FrostHuntConfig::IsDedicatedWorld(world) || ThunderdeepHuntConfig::IsDedicatedWorld(world)
        || (DojoProbe::FrostHuntEnabled() && hunt == "CR19_MatchmakerHunt_Beaver")) return true;
    // The open Trials prototype has no pursuit ID or hunt-results exit. Keep
    // the native return available even after its manually spawned target dies.
    // Replicated mode + exact world scope exclude cities and ordinary hunts.
    auto* mode = world->GameState->GameModeClass.Get();
    return !DojoProbe::FrostHuntEnabled() && (hunt.empty() || hunt == "None")
        && DojoProbe::ObjectPath(world) == "/Game/Maps/islands/arenas/arena_ramsgate_00.arena_ramsgate_00"
        && mode && DojoProbe::ObjectPath(mode) == "/Game/Blueprints/BPGM_Archon_Prototype.BPGM_Archon_Prototype_C";
}
inline void ClearTravel(const char* reason) {
    if (PendingTravel.World) Log("\"event\":\"confirmed_return_cleared\",\"confirmationId\":" + std::to_string(PendingTravel.Id)
        + ",\"reason\":" + DojoProbe::Json(reason));
    PendingTravel = {};
}
inline std::string ObjectPath(UObject* object) {
    if (!object) return {};
    auto* package = object;
    while (package->Outer) package = package->Outer;
    return package->Name.GetRawString() + (package == object ? std::string{} : "." + object->Name.GetRawString());
}
// The stock row filter queries the item view-model as world context. Read every
// form here, at menu creation, so a cached menu cannot conceal a later reset.
inline std::string FeatureState(UWorld* world, UObject* context, UClass* type) {
    if (!type) return "{\"available\":false}";
    const bool local = UArchonFeatureFlagBlueprintLibrary::IsLocalFeatureEnabled(type);
    auto* base = UObject::FindClassFast("ArchonFeature");
    auto* object = type->ClassDefaultObject;
    auto* feature = object && base && object->IsA(base) ? static_cast<UArchonFeature*>(object) : nullptr;
    return "{\"available\":true,\"class\":" + DojoProbe::Json(ObjectPath(type))
        + ",\"cdoEnabled\":" + DojoProbe::Bool(feature && feature->bEnabled)
        + ",\"local\":" + DojoProbe::Bool(local)
        + ",\"world\":" + DojoProbe::Bool(UArchonFeatureFlagBlueprintLibrary::IsFeatureEnabled(world, type))
        + ",\"worldContext\":" + DojoProbe::Bool(UArchonFeatureFlagBlueprintLibrary::IsFeatureEnabledWorldContext(world, type))
        + ",\"itemContext\":" + DojoProbe::Bool(UArchonFeatureFlagBlueprintLibrary::IsFeatureEnabled(context, type)) + '}';
}
inline bool __fastcall ReturnInitialize(UMainMenuReturnToRamsgateItemViewModel* item, AArchonPlayerController* player) {
    const bool initialized = OriginalReturnInitialize(item, player);
    auto* world = ControllerWorld(player);
    if (!initialized || !item || !IsLabReturnWorld(world)) return initialized;
    if (item->ItemData.TelemetryID.ToString() != "return_ramsgate") {
        Log("\"event\":\"return_item_guard_rejected\",\"reason\":\"unexpected_telemetry\"");
        return initialized;
    }
    if (!item->ItemData.bShowInUI) return initialized;
    auto* modelType = UObject::FindClassFast("MainMenuViewModel");
    auto* model = item->Outer && modelType && item->Outer->IsA(modelType)
        ? static_cast<UMainMenuViewModel*>(item->Outer) : nullptr;
    if (!model || (model->bIsInTutorial && !item->ItemData.bShowDuringTutorial)) return initialized;
    auto* featureType = UObject::FindClassFast("ReturnToRamsgateFeature");
    if (!featureType || !UArchonFeatureFlagBlueprintLibrary::IsFeatureEnabled(item, featureType)) return initialized;
    const auto flags = item->ItemData.RequiredFeatureFlags.NumAllocated();
    if (flags < 0 || flags > 16) return initialized;
    for (int32 index = 0; index < flags; ++index) if (item->ItemData.RequiredFeatureFlags.IsValidIndex(index)) {
        const auto& flag = item->ItemData.RequiredFeatureFlags[index];
        UClass* required = flag.Key();
        if (!required || UArchonFeatureFlagBlueprintLibrary::IsFeatureEnabled(item, required) != flag.Value()) return initialized;
    }
    // 1.4.4's native Return subclass hides this item outside Activities mode,
    // even after the standard row/feature checks pass. Restore only this lab
    // hunt's visibility before InitPanelViewModelsRecursive filters the list.
    // The original initializer, click action and confirmation/travel remain.
    const bool nativeShow = item->bShowInUI;
    item->bShowInUI = true;
    if (ReturnBindings.size() >= 16) ReturnBindings.clear();
    ReturnBindings[item->Index] = {item, player, player->Index, world};
    Log("\"event\":\"return_item_initialized\",\"map\":" + DojoProbe::Json(world->GetName())
        + ",\"itemIndex\":" + std::to_string(item->Index) + ",\"nativeShow\":" + DojoProbe::Bool(nativeShow)
        + ",\"show\":true,\"feature\":" + FeatureState(world, item, featureType));
    return initialized;
}
// This callback is only invoked by the popup's true/Yes branch. Its original
// body starts native city matchmaking; no replacement request is issued here.
inline void __fastcall ReturnConfirmed(UMainMenuReturnToRamsgateItemViewModel* item) {
    ReturnBinding binding{};
    bool eligible = false;
    if (item) {
        const auto found = ReturnBindings.find(item->Index);
        if (found != ReturnBindings.end() && found->second.Item == item) {
            binding = found->second;
            eligible = binding.Player && UObject::GObjects->GetByIndex(binding.PlayerIndex) == binding.Player
                && ControllerWorld(binding.Player) == binding.World && IsLabReturnWorld(binding.World)
                && item->ItemData.TelemetryID.ToString() == "return_ramsgate";
        }
    }
    if (eligible) ClearTravel("new_confirmation");
    OriginalReturnConfirmed(item);
    if (!eligible || ControllerWorld(binding.Player) != binding.World || !IsLabReturnWorld(binding.World)) return;
    auto* session = UArchonGameplayStatics::GetArchonOnlineSessionClient(binding.Player);
    auto* party = binding.Player->GetParty();
    if (!session || !party) {
        Log("\"event\":\"confirmed_return_rejected\",\"reason\":\"missing_native_session_or_party\"");
        return;
    }
    float expires = 0;
    const bool completed = session->IsCityMatchmakingSessionCompleted(&expires);
    PendingTravel = {};
    PendingTravel.World = binding.World; PendingTravel.Player = binding.Player;
    PendingTravel.Session = session; PendingTravel.Party = party;
    PendingTravel.PlayerIndex = binding.PlayerIndex; PendingTravel.SessionIndex = session->Index; PendingTravel.PartyIndex = party->Index;
    PendingTravel.Id = ++NextConfirmationId; PendingTravel.Deadline = GetTickCount64() + 45000;
    PendingTravel.SawIncomplete = !completed;
    Log("\"event\":\"confirmed_return_armed\",\"confirmationId\":" + std::to_string(PendingTravel.Id)
        + ",\"completedAfterCallback\":" + DojoProbe::Bool(completed)
        + ",\"requiresFreshCompletion\":true");
}
inline void __fastcall CancelMatchmaking(AArchonPartyClient* party) {
    if (PendingTravel.World && PendingTravel.Party == party) ClearTravel("native_matchmaking_cancelled");
    OriginalCancelMatchmaking(party);
}
inline void TickTravel(UWorld* world) {
    if (!PendingTravel.World || GetTickCount64() < PendingTravel.NextPoll) return;
    PendingTravel.NextPoll = GetTickCount64() + 100;
    if (world != PendingTravel.World || !IsLabReturnWorld(world)) { ClearTravel("world_changed"); return; }
    if (UObject::GObjects->GetByIndex(PendingTravel.PlayerIndex) != PendingTravel.Player
        || UObject::GObjects->GetByIndex(PendingTravel.SessionIndex) != PendingTravel.Session
        || UObject::GObjects->GetByIndex(PendingTravel.PartyIndex) != PendingTravel.Party
        || PendingTravel.Player->GetParty() != PendingTravel.Party
        || ControllerWorld(PendingTravel.Player) != world) { ClearTravel("native_objects_changed"); return; }
    if (GetTickCount64() >= PendingTravel.Deadline) { ClearTravel("fresh_completion_timeout"); return; }
    float expires = 0;
    const bool completed = PendingTravel.Session->IsCityMatchmakingSessionCompleted(&expires);
    if (!completed) {
        if (!PendingTravel.SawIncomplete) Log("\"event\":\"confirmed_return_fresh_request_pending\",\"confirmationId\":" + std::to_string(PendingTravel.Id));
        PendingTravel.SawIncomplete = true;
        return;
    }
    // Require the native session to have been incomplete after this actual Yes
    // callback, then completed. Never consume a cached allocation from login or
    // an earlier request merely because it is still marked complete.
    if (!PendingTravel.SawIncomplete) {
        if (!PendingTravel.ReportedCached) Log("\"event\":\"confirmed_return_waiting_for_fresh_session\",\"confirmationId\":" + std::to_string(PendingTravel.Id));
        PendingTravel.ReportedCached = true;
        return;
    }
    // Native completion is the reservation-valid predicate. The output is a
    // separate countdown and may legitimately reach zero while still valid.
    if (!std::isfinite(expires) || expires < 0) { ClearTravel("invalid_native_city_countdown"); return; }
    auto* player = PendingTravel.Player;
    auto* session = PendingTravel.Session;
    const auto confirmation = PendingTravel.Id;
    PendingTravel = {}; // Consume before native travel can re-enter a tick.
    Log("\"event\":\"confirmed_return_native_city_travel\",\"confirmationId\":" + std::to_string(confirmation)
        + ",\"freshCompletion\":true,\"nativeCountdown\":" + std::to_string(expires));
    session->TravelToCompletedCityMatchmakingSession(player);
}
// Called once from the isolated client hook setup, after MH_Initialize.
inline bool Install(uintptr_t base) {
    if (ReturnHookInstalled) return true;
    if (!DojoProbe::IsExpectedProcess() || std::wstring(GetCommandLineW()).find(L"-server") != std::wstring::npos) return false;
    // Independently traced from the stock MainMenuReturnToRamsgateItemViewModel
    // vtable (virtual slot 0x260), its diagnostic name and .pdata boundaries.
    constexpr uintptr_t rva = 0x18DF100;
    const uint8_t prologue[] = {0x48,0x89,0x5C,0x24,0x20,0x55,0x57,0x41,0x56,0x48,0x8D,0xAC,0x24,0x70,0xFF,0xFF,0xFF};
    auto* target = reinterpret_cast<void*>(base + rva);
    auto* confirmTarget = reinterpret_cast<void*>(base + 0x18F08A0);
    const uint8_t confirmPrologue[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x81,0xEC,0x60,0x01,0x00,0x00};
    auto* cancelTarget = reinterpret_cast<void*>(base + 0x1599110);
    const uint8_t cancelPrologue[] = {0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x57,0x41,0x57,0x48,0x8D,0x6C,0x24,0xB9};
    if (std::memcmp(target, prologue, sizeof prologue) || std::memcmp(confirmTarget, confirmPrologue, sizeof confirmPrologue)
        || std::memcmp(cancelTarget, cancelPrologue, sizeof cancelPrologue)) {
        Log("\"event\":\"return_initializer_hook_failed\",\"reason\":\"native_prologue_mismatch\"");
        return false;
    }
    struct Hook { void* Target; void* Detour; void** Original; };
    const Hook hooks[] = {
        {target, reinterpret_cast<void*>(&ReturnInitialize), reinterpret_cast<void**>(&OriginalReturnInitialize)},
        {confirmTarget, reinterpret_cast<void*>(&ReturnConfirmed), reinterpret_cast<void**>(&OriginalReturnConfirmed)},
        {cancelTarget, reinterpret_cast<void*>(&CancelMatchmaking), reinterpret_cast<void**>(&OriginalCancelMatchmaking)}
    };
    int created = 0;
    for (const auto& hook : hooks) {
        const auto status = MH_CreateHook(hook.Target, hook.Detour, hook.Original);
        if (status != MH_OK) {
            for (int index = 0; index < created; ++index) MH_RemoveHook(hooks[index].Target);
            OriginalReturnInitialize = nullptr; OriginalReturnConfirmed = nullptr; OriginalCancelMatchmaking = nullptr;
            Log("\"event\":\"return_initializer_hook_failed\",\"reason\":\"create_hook\",\"hook\":" + std::to_string(created)
                + ",\"status\":" + std::to_string(status));
            return false;
        }
        ++created;
    }
    for (const auto& hook : hooks) {
        const auto status = MH_EnableHook(hook.Target);
        if (status != MH_OK) {
            for (const auto& rollback : hooks) { MH_DisableHook(rollback.Target); MH_RemoveHook(rollback.Target); }
            OriginalReturnInitialize = nullptr; OriginalReturnConfirmed = nullptr; OriginalCancelMatchmaking = nullptr;
            Log("\"event\":\"return_initializer_hook_failed\",\"reason\":\"enable_hook\",\"status\":" + std::to_string(status));
            return false;
        }
    }
    ReturnHookInstalled = true;
    Log("\"event\":\"return_initializer_hook_installed\",\"rva\":\"0x18DF100\",\"confirmationRva\":\"0x18F08A0\",\"cancelRva\":\"0x1599110\"");
    return true;
}
inline std::string RootTableState(UWorld* world, UMainMenuViewModel* model) {
    auto* table = model->RootPanelData;
    std::string result = "{\"path\":" + DojoProbe::Json(ObjectPath(table));
    if (!table || !table->RowStruct || table->RowStruct->GetName() != "MainMenuItemData")
        return result + ",\"validRowStruct\":false}";
    const auto allocated = table->RowMap.NumAllocated();
    result += ",\"validRowStruct\":true,\"rowCount\":" + std::to_string(table->RowMap.Num());
    if (allocated < 0 || allocated > 64) return result + ",\"bounded\":false}";
    result += ",\"bounded\":true,\"returnRows\":[";
    bool first = true;
    for (int32 index = 0; index < allocated; ++index) if (table->RowMap.IsValidIndex(index)) {
        auto* row = reinterpret_cast<FMainMenuItemData*>(table->RowMap[index].Value());
        if (!row || row->TelemetryID.ToString() != "return_ramsgate") continue;
        if (!first) result += ',';
        first = false;
        result += "{\"rowName\":" + DojoProbe::Json(table->RowMap[index].Key().ToString())
            + ",\"show\":" + DojoProbe::Bool(row->bShowInUI)
            + ",\"showDuringTutorial\":" + DojoProbe::Bool(row->bShowDuringTutorial)
            + ",\"disableDuringHunts\":" + DojoProbe::Bool(row->bDisableDuringHunts)
            + ",\"viewModelClass\":" + DojoProbe::Json(ObjectPath(row->ViewModelClassOverride))
            + ",\"requiredFeatures\":[";
        const auto flags = row->RequiredFeatureFlags.NumAllocated();
        bool firstFlag = true;
        if (flags >= 0 && flags <= 16) for (int32 flag = 0; flag < flags; ++flag) if (row->RequiredFeatureFlags.IsValidIndex(flag)) {
            if (!firstFlag) result += ',';
            firstFlag = false;
            result += "{\"expected\":" + std::string(DojoProbe::Bool(row->RequiredFeatureFlags[flag].Value()))
                + ",\"state\":" + FeatureState(world, model, row->RequiredFeatureFlags[flag].Key()) + '}';
        }
        result += "]}";
    }
    return result + "]}";
}
inline std::string FilteredReturnState(UWorld* world, UMainMenuViewModel* model) {
    auto* type = UObject::FindClassFast("MainMenuReturnToRamsgateItemViewModel");
    auto* featureType = UObject::FindClassFast("ReturnToRamsgateFeature");
    std::string result = "[";
    int count = 0;
    // Native InitPanelViewModelsRecursive creates each item with Outer == model,
    // including the rows it subsequently excludes from MenuItemViewModels.
    if (type) for (int32 index = 0; index < UObject::GObjects->Num() && count < 4; ++index) {
        auto* object = UObject::GObjects->GetByIndex(index);
        if (!object || object->Outer != model || !object->IsA(type) || object->IsDefaultObject()) continue;
        auto* item = static_cast<UMainMenuItemViewModel*>(object);
        if (count++) result += ',';
        result += "{\"class\":" + DojoProbe::Json(item->Class->GetName())
            + ",\"show\":" + DojoProbe::Bool(item->bShowInUI) + ",\"locked\":" + DojoProbe::Bool(item->bIsLocked)
            + ",\"rowShow\":" + DojoProbe::Bool(item->ItemData.bShowInUI)
            + ",\"feature\":" + FeatureState(world, item, featureType) + '}';
    }
    return result + ']';
}
// Read-only: inspect the real menu model instead of inferring it from the feature flag.
inline void MenuDiagnostics(UWorld* world) {
    if (!world || GetTickCount64() < NextMenuPoll) return;
    NextMenuPoll = GetTickCount64() + 1000;
    const auto map = world->GetName();
    if (MenuWorld != world || MenuMap != map) {
        MenuWorld = world; MenuMap = map; MenuSnapshots.clear(); LastMenuScreenCount = -1;
    }
    auto* screenType = UObject::FindClassFast("MainMenuScreen");
    auto* itemType = UObject::FindClassFast("MainMenuItemViewModel");
    auto* libraryType = UObject::FindClassFast("WidgetBlueprintLibrary");
    auto* library = libraryType ? libraryType->ClassDefaultObject : nullptr;
    auto* function = DojoProbe::FindFunction(library, "GetAllWidgetsOfClass");
    if (!screenType || !itemType || !library || !function) return;
    Params::WidgetBlueprintLibrary_GetAllWidgetsOfClass query{};
    query.WorldContextObject = world; query.WidgetClass = screenType; query.TopLevelOnly = false;
    library->ProcessEvent(function, &query);
    if (query.FoundWidgets.Num() < 0 || query.FoundWidgets.Num() > 16) return;
    if (LastMenuScreenCount != query.FoundWidgets.Num()) {
        LastMenuScreenCount = query.FoundWidgets.Num();
        Log("\"event\":\"main_menu_screens\",\"map\":" + DojoProbe::Json(map)
            + ",\"screenCount\":" + std::to_string(LastMenuScreenCount));
    }
    static const std::set<std::string> canonicalIds = {
        "return_ramsgate", "hunts", "inventory", "player_journey_map", "quests_and_progression",
        "hunt_pass", "store", "social", "inbox", "move_list", "options", "help", "quit"
    };
    for (auto* widget : query.FoundWidgets) {
        auto* screen = static_cast<UMainMenuScreen*>(widget);
        auto* model = screen ? screen->MainMenuViewModel : nullptr;
        if (!model || model->MenuItemViewModels.Num() < 0 || model->MenuItemViewModels.Num() > 64) continue;
        std::string rows = "[";
        bool first = true, hasReturn = false;
        int skipped = 0;
        for (auto* entry : model->MenuItemViewModels) {
            if (!entry || !entry->IsA(itemType)) { ++skipped; continue; }
            auto* item = static_cast<UMainMenuItemViewModel*>(entry);
            const auto id = item->ItemData.TelemetryID.ToString();
            if (!canonicalIds.contains(id)) { ++skipped; continue; }
            if (!first) rows += ',';
            first = false;
            hasReturn = hasReturn || id == "return_ramsgate";
            rows += "{\"telemetryId\":" + DojoProbe::Json(id) + ",\"class\":" + DojoProbe::Json(item->Class->GetName())
                + ",\"show\":" + DojoProbe::Bool(item->bShowInUI) + ",\"locked\":" + DojoProbe::Bool(item->bIsLocked) + '}';
        }
        rows += ']';
        const auto snapshot = "\"screenClass\":" + DojoProbe::Json(screen->Class->GetName())
            + ",\"modelClass\":" + DojoProbe::Json(model->Class->GetName())
            + ",\"menuListCount\":" + std::to_string(model->MenuItemViewModels.Num())
            + ",\"inHunt\":" + DojoProbe::Bool(model->bIsInAHunt) + ",\"tutorial\":" + DojoProbe::Bool(model->bIsInTutorial)
            + ",\"hasReturnRow\":" + DojoProbe::Bool(hasReturn) + ",\"skippedNoncanonicalRows\":" + std::to_string(skipped)
            + ",\"returnFeature\":" + FeatureState(world, model, UObject::FindClassFast("ReturnToRamsgateFeature"))
            + ",\"rootPanelData\":" + RootTableState(world, model)
            + ",\"filteredReturnItems\":" + FilteredReturnState(world, model)
            + ",\"rows\":" + rows;
        const auto previous = MenuSnapshots.find(model->Index);
        if (previous != MenuSnapshots.end() && previous->second == snapshot) continue;
        if (MenuSnapshots.size() >= 16 && previous == MenuSnapshots.end()) continue;
        MenuSnapshots[model->Index] = snapshot;
        Log("\"event\":\"main_menu_model\",\"map\":" + DojoProbe::Json(map)
            + ",\"modelIndex\":" + std::to_string(model->Index) + ',' + snapshot);
    }
}
// Call after the earliest client engine tick, before FrostEncounter::TickClient diagnostics.
// MainMenuViewModel filters its root rows at construction, so waiting until an
// arena has replicated is too late. Keep this single gameplay feature enabled
// for this isolated client lifetime, including its city/menu construction.
// The regular native menu, confirmation and matchmaking still perform the return.
inline void Tick(UWorld* world) {
    if (!DojoProbe::IsExpectedProcess() || std::wstring(GetCommandLineW()).find(L"-server") != std::wstring::npos || !world) return;
    TickTravel(world);
    MenuDiagnostics(world);
    if (Active || FailedWorld == world || GetTickCount64() < NextPoll) return;
    NextPoll = GetTickCount64() + 1000;
    auto* type = UObject::FindClassFast("ReturnToRamsgateFeature");
    auto* base = UObject::FindClassFast("ArchonFeature");
    if (!type || !base) return;
    // The native query materializes the feature's default object if needed.
    const bool localBefore = UArchonFeatureFlagBlueprintLibrary::IsLocalFeatureEnabled(type);
    auto* object = type ? type->ClassDefaultObject : nullptr;
    if (!object || !base || !object->IsA(base)) return;
    auto* feature = static_cast<UArchonFeature*>(object);
    const auto branches = feature->PerBranchSettings.NumAllocated(), environments = feature->PerEnvironmentSettings.NumAllocated();
    if (branches < 0 || branches > 64 || environments < 0 || environments > 256) {
        FailedWorld = world;
        Log("\"event\":\"native_return_feature_failed\",\"reason\":\"feature_override_map_bounds\"");
        return;
    }
    const bool before = UArchonFeatureFlagBlueprintLibrary::IsFeatureEnabled(world, type);
    Feature = feature; FeatureIndex = feature->Index; OriginalEnabled = feature->bEnabled;
    OriginalBranches.clear(); OriginalEnvironments.clear();
    for (int32 index = 0; index < branches; ++index) if (feature->PerBranchSettings.IsValidIndex(index)) {
        auto& setting = feature->PerBranchSettings[index];
        OriginalBranches.push_back({index, setting.Key(), setting.Value()});
        setting.Value() = true;
    }
    for (int32 index = 0; index < environments; ++index) if (feature->PerEnvironmentSettings.IsValidIndex(index)) {
        auto& setting = feature->PerEnvironmentSettings[index];
        OriginalEnvironments.push_back({index, setting.Key().ToString(), setting.Value()});
        setting.Value() = true;
    }
    feature->bEnabled = true;
    Active = true;
    const bool after = UArchonFeatureFlagBlueprintLibrary::IsFeatureEnabled(world, type);
    const bool worldContextAfter = UArchonFeatureFlagBlueprintLibrary::IsFeatureEnabledWorldContext(world, type);
    Log("\"event\":\"native_return_feature_enabled\",\"map\":" + DojoProbe::Json(world->GetName())
        + ",\"earlyLabClient\":true,\"localBefore\":" + DojoProbe::Bool(localBefore)
        + ",\"before\":" + DojoProbe::Bool(before) + ",\"after\":" + DojoProbe::Bool(after)
        + ",\"worldContextAfter\":" + DojoProbe::Bool(worldContextAfter)
        + ",\"originalEnabled\":" + DojoProbe::Bool(OriginalEnabled)
        + ",\"branchOverrides\":" + std::to_string(OriginalBranches.size())
        + ",\"environmentOverrides\":" + std::to_string(OriginalEnvironments.size()));
}
}
