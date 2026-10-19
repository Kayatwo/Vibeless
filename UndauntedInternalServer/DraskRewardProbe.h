// Opt-in, read-only Thunderdeep144 part/loot evidence. AGPL-3.0-only.
#pragma once
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include "DojoProbe.h"
#include "ThunderdeepHuntConfig.h"
#include "SDK/monsterpart_bpc_classes.hpp"
#include "SDK/monsterpart_bpc_parameters.hpp"
#include "SDK/loot_dropper_bpc_classes.hpp"
#include "SDK/loot_dropper_bpc_parameters.hpp"
#include "SDK/Archon_parameters.hpp"

namespace DraskRewardProbe {
using namespace SDK;
inline constexpr unsigned MaxEvents = 4096;
inline constexpr int MaxParts = 32, MaxStages = 16, MaxItems = 64;
inline UWorld* ActiveWorld = nullptr;
inline int ActivePort = 0;
inline DWORD EngineThread = 0;
inline ULONGLONG NextPoll = 0;
inline unsigned EventCount = 0;
inline bool SawDrask = false, InReadGetter = false;
struct PartState {
    int Index = -1, MonsterIndex = -1, Stage = -1, HealthBucket = -1;
    bool Severed = false, PositiveHealth = false, DamageObserved = false;
    float LastHealth = 0;
    std::string State;
};
inline std::map<UObject*, PartState> Parts;
inline std::map<UObject*, int> Droppers;
inline std::set<UObject*> Inventories, Players;
inline std::set<int> ReportedMonsters;
inline std::map<int, int> MonsterHealthBuckets;
inline std::string UtcNow() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char value[32]{};
    std::snprintf(value, sizeof(value), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth), static_cast<unsigned>(time.wDay),
        static_cast<unsigned>(time.wHour), static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond), static_cast<unsigned>(time.wMilliseconds));
    return value;
}

inline bool Enabled() {
    static const bool enabled = [] {
        if (!DojoProbe::IsExpectedProcess()) return false;
        const auto path = std::filesystem::path(DojoProbe::Root) / L"data/drask-reward-probe.ini";
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > 4096) return false;
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line)) {
            const auto split = line.find('=');
            if (split != std::string::npos && DojoProbe::Trim(line.substr(0, split)) == "Enabled")
                return DojoProbe::Trim(line.substr(split + 1)) == "1";
        }
        return false;
    }();
    return enabled;
}
inline void Log(const std::string& body) {
    if (EventCount >= MaxEvents) return;
    ++EventCount;
    std::ofstream out(std::filesystem::path(DojoProbe::Root) / L"reports/drask-rewards.jsonl", std::ios::app);
    out << "{\"version\":1,\"pid\":" << GetCurrentProcessId() << ",\"uptimeMs\":" << GetTickCount64()
        << ",\"port\":" << ActivePort << ',' << body << "}\n";
}
inline std::string Label(const FName& value) { return DojoProbe::Json(value.ToString().substr(0, 256)); }
inline std::string Number(float value) { return std::isfinite(value) ? std::to_string(value) : "null"; }
template<class T> inline bool Bounded(const TArray<T>& array, int limit) {
    return array.Num() >= 0 && array.Num() <= limit && array.Max() >= array.Num()
        && (array.Num() == 0 || array.IsValidIndex(array.Num() - 1));
}
inline bool IsPortedDrask(AActor* actor) {
    return actor && actor->Class && !actor->IsActorBeingDestroyed() && actor->HasAuthority()
        && DojoProbe::ObjectPath(actor->Class) == ThunderdeepHuntConfig::MonsterPath;
}
inline std::string Items(const TArray<FGiveItemInfo>& items) {
    if (!Bounded(items, MaxItems)) return "null";
    std::string json = "[";
    for (int i = 0; i < items.Num(); ++i) {
        if (i) json += ',';
        json += "{\"item\":" + Label(items[i].Item) + ",\"amount\":" + std::to_string(items[i].Amount) + "}";
    }
    return json + "]";
}
inline std::string LootMember(UObject* object, const char* name, bool stringValue = false) {
    unsigned depth = 0, fields = 0;
    for (auto* type = static_cast<UStruct*>(object->Class); type && ++depth <= 32; type = type->SuperStruct)
        for (auto* field = type->ChildProperties; field && ++fields <= 2048; field = field->Next) {
            if (field->Name.ToString() != name) continue;
            auto* property = static_cast<FProperty*>(field);
            const int size = stringValue ? sizeof(FString) : sizeof(FName);
            const auto kind = field->ClassPrivate ? field->ClassPrivate->Name.ToString() : "";
            if (kind != (stringValue ? "StrProperty" : "NameProperty") || property->ElementSize != size
                || property->ArrayDim != 1 || property->Offset < 0 || object->Class->Size > 1024 * 1024
                || property->Offset > object->Class->Size - size) return "null";
            const auto* address = reinterpret_cast<const uint8*>(object) + property->Offset;
            std::string value;
            if (stringValue) {
                const auto& text = *reinterpret_cast<const FString*>(address);
                if (!Bounded<wchar_t>(text, 256)) return "null";
                for (int i = 0; i < text.Num() && text[i]; ++i) value += static_cast<char>(text[i]);
            } else value = reinterpret_cast<const FName*>(address)->ToString();
            return "{\"type\":" + DojoProbe::Json(kind) + ",\"offset\":" + std::to_string(property->Offset)
                + ",\"value\":" + DojoProbe::Json(value) + "}";
        }
    return "null";
}
inline std::string PartArrays(Umonsterpart_bpc_C* part) {
    if (!Bounded(part->LootIDs, MaxStages) || !Bounded(part->LootDropsPerStage, MaxStages)) return "null";
    std::string json = "{\"lootIds\":[";
    for (int i = 0; i < part->LootIDs.Num(); ++i) { if (i) json += ','; json += Label(part->LootIDs[i]); }
    json += "],\"dropsPerStage\":[";
    for (int i = 0; i < part->LootDropsPerStage.Num(); ++i) {
        if (i) json += ',';
        json += std::to_string(part->LootDropsPerStage[i]);
    }
    return json + "]}";
}
// These are existing old Blueprint read getters, never break/drop/grant entry points.
inline std::string LootDefinition(Umonsterpart_bpc_C* part) {
    if (!Bounded(part->PartBreakData, MaxStages)) return "null";
    InReadGetter = true;
    struct { FString BaseLootName; } baseParams{};
    if (auto* function = DojoProbe::FindFunction(part, "GetBaseLootName")) part->ProcessEvent(function, &baseParams);
    const auto& base = baseParams.BaseLootName;
    auto* breakFunction = DojoProbe::FindFunction(part, "GetBreakLootName");
    std::string baseName;
    if (Bounded<wchar_t>(base, 512)) {
        for (int i = 0; i < base.Num() && base[i]; ++i) baseName += static_cast<char>(base[i]);
    }
    std::string json = "{\"baseLootName\":" + DojoProbe::Json(baseName) + ",\"stages\":[";
    for (int i = 0; i < part->PartBreakData.Num(); ++i) {
        struct { int32 BreakIndex; FName LootName; } breakParams{};
        static_assert(offsetof(decltype(breakParams), LootName) == 4);
        breakParams.BreakIndex = i;
        if (breakFunction) part->ProcessEvent(breakFunction, &breakParams);
        if (i) json += ',';
        const auto& stage = part->PartBreakData[i];
        json += "{\"index\":" + std::to_string(i) + ",\"lootName\":" + Label(breakParams.LootName)
            + ",\"maxDrops\":" + std::to_string(stage.MaxLootDrops_57_81F772BD48826EC5FEBB70A860F2BA41)
            + ",\"description\":" + std::to_string(static_cast<int>(stage.PartBreakDescription_81_F02FC7404C14B0C4C604A5B52827B0C9)) + "}";
    }
    InReadGetter = false;
    return json + "]}";
}
inline void SamplePart(Umonsterpart_bpc_C* part, AArchonBehemoth* monster) {
    auto found = Parts.find(part);
    const bool initial = found == Parts.end() || found->second.Index != part->Index;
    const auto arrays = PartArrays(part);
    const float health = part->UMonsterPartComponent::GetCurrentHealth();
    const float maximum = part->UMonsterPartComponent::GetMaxHealth();
    const int bucket = std::isfinite(health) && std::isfinite(maximum) && maximum > 0
        ? static_cast<int>(std::clamp(health / maximum, 0.0f, 1.0f) * 10.0f) : -1;
    const auto stateKey = std::to_string(part->CurrentBreakStage) + ":" + DojoProbe::Bool(part->PartSevered) + ":" + arrays;
    const bool hadPositiveHealth = !initial && found->second.PositiveHealth;
    const bool damageObserved = !initial && (found->second.DamageObserved
        || (std::isfinite(health) && std::isfinite(found->second.LastHealth) && health + 0.01f < found->second.LastHealth));
    if (!initial && found->second.State == stateKey && found->second.HealthBucket == bucket) {
        found->second.PositiveHealth = hadPositiveHealth || (std::isfinite(health) && health > 0);
        found->second.DamageObserved = damageObserved;
        found->second.LastHealth = health;
        return;
    }
    const bool stageTransition = !initial && (part->CurrentBreakStage > found->second.Stage
        || (part->PartSevered && !found->second.Severed));
    const bool breakObserved = stageTransition && hadPositiveHealth && damageObserved;
    std::string body = "\"event\":" + DojoProbe::Json(breakObserved ? "part_break_observed" : stageTransition ? "part_stage_transition" : "part_sample")
        + ",\"initial\":" + DojoProbe::Bool(initial) + ",\"monsterIndex\":" + std::to_string(monster->Index)
        + ",\"partIndex\":" + std::to_string(part->Index) + ",\"component\":" + DojoProbe::Json(part->GetName())
        + ",\"partName\":" + Label(part->PartName) + ",\"stage\":" + std::to_string(part->CurrentBreakStage)
        + ",\"previousStage\":" + (initial ? "null" : std::to_string(found->second.Stage))
        + ",\"priorPositiveHealth\":" + DojoProbe::Bool(hadPositiveHealth) + ",\"priorDamageObserved\":" + DojoProbe::Bool(damageObserved)
        + ",\"severed\":" + DojoProbe::Bool(part->PartSevered) + ",\"partHealth\":" + Number(health)
        + ",\"partMaxHealth\":" + Number(maximum) + ",\"monsterHealth\":" + Number(monster->GetCurrentHealth())
        + ",\"lootState\":" + arrays;
    if (initial) body += ",\"lootDefinition\":" + LootDefinition(part);
    Log(body);
    Parts[part] = {part->Index, monster->Index, part->CurrentBreakStage, bucket, part->PartSevered,
        hadPositiveHealth || (std::isfinite(health) && health > 0), damageObserved, health, stateKey};
}
inline void Reset(UWorld* world, int port) {
    ActiveWorld = world;
    ActivePort = port;
    SawDrask = false;
    Parts.clear(); Droppers.clear(); Inventories.clear(); Players.clear(); ReportedMonsters.clear(); MonsterHealthBuckets.clear();
    auto* stateType = UObject::FindClassFast("ArchonGameState");
    const auto huntId = world && world->GameState && stateType && world->GameState->IsA(stateType)
        ? static_cast<AArchonGameState*>(world->GameState)->MatchmakerHuntID.ToString() : std::string{};
    Log("\"event\":\"world\",\"map\":" + DojoProbe::Json(world ? world->GetName() : "")
        + ",\"utc\":" + DojoProbe::Json(UtcNow()) + ",\"matchmakerHuntId\":" + DojoProbe::Json(huntId)
        + ",\"readOnly\":true,\"pollMs\":500,\"nativeCallbacksMayBypassProcessEvent\":true");
}
// Root calls from its existing server engine tick. No worker thread touches engine objects.
inline void TickServer(UWorld* world, UNetDriver* driver, int port) {
    if (!Enabled()) return;
    if (!world || !driver || driver->World != world || world->NetDriver != driver || driver->ServerConnection
        || !ThunderdeepHuntConfig::IsDedicatedServer(world, port) || !world->AuthorityGameMode || !world->AuthorityGameMode->HasAuthority()) {
        if (ActiveWorld) Reset(nullptr, 0);
        return;
    }
    if (!EngineThread) EngineThread = GetCurrentThreadId();
    if (EngineThread != GetCurrentThreadId()) return;
    if (ActiveWorld != world || ActivePort != port) { Reset(world, port); NextPoll = 0; }
    const auto now = GetTickCount64();
    if (now < NextPoll || EventCount >= MaxEvents) return;
    NextPoll = now + 500;
    auto* partClass = UObject::FindClassFast("monsterpart_bpc_C");
    auto* dropperClass = UObject::FindClassFast("loot_dropper_bpc_C");
    if (!partClass) return;
    TArray<AActor*> actors;
    UGameplayStatics::GetAllActorsOfClass(world, AArchonBehemoth::StaticClass(), &actors);
    if (!Bounded(actors, 32)) return;
    std::set<UObject*> currentParts, currentDroppers;
    unsigned count = 0;
    for (auto* actor : actors) {
        if (!IsPortedDrask(actor) || ++count > 4) continue;
        SawDrask = true;
        auto* monster = static_cast<AArchonBehemoth*>(actor);
        const float monsterHealth = monster->GetCurrentHealth(), monsterMaximum = monster->GetMaxHealth();
        const int monsterBucket = std::isfinite(monsterHealth) && std::isfinite(monsterMaximum) && monsterMaximum > 0
            ? static_cast<int>(std::clamp(monsterHealth / monsterMaximum, 0.0f, 1.0f) * 100.0f) : -1;
        if (!MonsterHealthBuckets.count(monster->Index) || MonsterHealthBuckets[monster->Index] != monsterBucket) {
            MonsterHealthBuckets[monster->Index] = monsterBucket;
            Log("\"event\":\"monster_sample\",\"monsterIndex\":" + std::to_string(monster->Index)
                + ",\"health\":" + Number(monsterHealth) + ",\"maximum\":" + Number(monsterMaximum)
                + ",\"dead\":" + DojoProbe::Bool(std::isfinite(monsterHealth) && monsterHealth <= 0));
        }
        if (ReportedMonsters.insert(monster->Index).second)
            Log("\"event\":\"monster_loot_definition\",\"monsterIndex\":" + std::to_string(monster->Index)
                + ",\"classPath\":" + DojoProbe::Json(DojoProbe::ObjectPath(monster->Class))
                + ",\"archetype\":" + LootMember(monster, "ArchetypeName")
                + ",\"classification\":" + LootMember(monster, "ClassificationName")
                + ",\"classificationLootOverride\":" + LootMember(monster, "ClassificationLootNameOverride")
                + ",\"eomLoot\":" + LootMember(monster, "EoM_LootID", true));
        const auto parts = monster->K2_GetComponentsByClass(partClass);
        if (Bounded(parts, MaxParts)) for (auto* component : parts) {
            if (!component || !component->IsA(partClass)) continue;
            currentParts.insert(component);
            SamplePart(static_cast<Umonsterpart_bpc_C*>(component), monster);
        }
        if (dropperClass) {
            const auto droppers = monster->K2_GetComponentsByClass(dropperClass);
            if (Bounded(droppers, 4)) for (auto* component : droppers) {
                if (!component || !component->IsA(dropperClass)) continue;
                currentDroppers.insert(component);
                if (!Droppers.count(component)) {
                    auto* dropper = static_cast<Uloot_dropper_bpc_C*>(component);
                    Log("\"event\":\"loot_dropper\",\"monsterIndex\":" + std::to_string(monster->Index)
                        + ",\"componentIndex\":" + std::to_string(component->Index)
                        + ",\"behemothName\":" + Label(dropper->BehemothName)
                        + ",\"defaultLootRow\":" + Label(dropper->DefaultLoot.RowName));
                }
                Droppers[component] = component->Index;
            }
        }
    }
    for (auto it = Parts.begin(); it != Parts.end();) {
        if (!currentParts.count(it->first)) it = Parts.erase(it); else ++it;
    }
    for (auto it = Droppers.begin(); it != Droppers.end();) {
        if (!currentDroppers.count(it->first)) it = Droppers.erase(it); else ++it;
    }
    Inventories.clear(); Players.clear();
    if (SawDrask && Bounded(driver->ClientConnections, 4)) for (auto* connection : driver->ClientConnections) {
        if (!connection || !connection->PlayerController) continue;
        auto* player = connection->PlayerController;
        auto* type = UObject::FindClassFast("ArchonPlayerController");
        if (!type || !player->IsA(type)) continue;
        Players.insert(player);
        auto* inventory = static_cast<AArchonPlayerController*>(player)->Inventory;
        if (inventory) Inventories.insert(inventory);
    }
}
// Passive observer for the EXISTING ProcessEvent hook, before/after its original call.
// Only CPF_Parm fields are read. Generated Blueprint-local fields are never inspected.
// Native VM calls may bypass ProcessEvent: absence of an event does not prove no grant.
inline void ObserveProcessEvent(UObject* object, UFunction* function, void* parameters, bool after) {
    if (!Enabled() || InReadGetter || !ActiveWorld || !SawDrask || !object || !function || !parameters
        || EngineThread != GetCurrentThreadId() || EventCount >= MaxEvents) return;
    const auto name = function->GetName();
    const auto part = Parts.find(object);
    if (part != Parts.end() && object->Index == part->second.Index) {
        int stage = -1;
        if (name == "AuthAddLoot") stage = static_cast<Params::monsterpart_bpc_C_AuthAddLoot*>(parameters)->BreakIndex;
        else if (name == "AuthHandleBreak") stage = static_cast<Params::monsterpart_bpc_C_AuthHandleBreak*>(parameters)->NextStage;
        else return;
        Log("\"event\":\"part_callback\",\"function\":" + DojoProbe::Json(name)
            + ",\"after\":" + DojoProbe::Bool(after) + ",\"partIndex\":" + std::to_string(object->Index)
            + ",\"monsterIndex\":" + std::to_string(part->second.MonsterIndex) + ",\"requestedStage\":" + std::to_string(stage));
    } else if (Droppers.count(object) && object->Index == Droppers[object]) {
        if (name == "AuthTryDropLoot") {
            const auto* p = static_cast<Params::loot_dropper_bpc_C_AuthTryDropLoot*>(parameters);
            Log(std::string("\"event\":\"drop_request\",\"after\":") + DojoProbe::Bool(after) + ",\"lootName\":" + Label(p->_Optional_CustomLoot)
                + ",\"bodyPart\":" + std::to_string(static_cast<int>(p->_Optional_BrokenBodyPart)));
        } else if (name == "CreateLootForPlayers") {
            const auto* p = static_cast<Params::loot_dropper_bpc_C_CreateLootForPlayers*>(parameters);
            Log(std::string("\"event\":\"loot_roll_callback\",\"after\":") + DojoProbe::Bool(after) + ",\"lootName\":" + Label(p->LootID)
                + ",\"bodyPart\":" + std::to_string(static_cast<int>(p->BrokenBodyPart)));
        }
    } else if (Inventories.count(object) && function->Outer == AArchonInventory::StaticClass()) {
        const TArray<FGiveItemInfo>* items = nullptr;
        if (name == "GrantItemsWithInfo") items = &static_cast<Params::ArchonInventory_GrantItemsWithInfo*>(parameters)->GiveItemInfos;
        else if (name == "ServerGrantItems") items = &static_cast<Params::ArchonInventory_ServerGrantItems*>(parameters)->ItemsToAdd;
        else if (name == "UnpackItems") {
            const auto* p = static_cast<Params::ArchonInventory_UnpackItems*>(parameters);
            items = after ? &p->ReturnValue : &p->ItemList;
        }
        if (items) Log("\"event\":\"inventory_callback\",\"function\":" + DojoProbe::Json(name)
            + ",\"after\":" + DojoProbe::Bool(after) + ",\"items\":" + Items(*items));
    } else if (name == "AddPendingLootForPlayer" && function->Outer == UPlayerLootBlueprintLibrary::StaticClass()) {
        const auto* p = static_cast<Params::PlayerLootBlueprintLibrary_AddPendingLootForPlayer*>(parameters);
        if (Players.count(p->Player)) Log(std::string("\"event\":\"pending_loot_callback\",\"after\":") + DojoProbe::Bool(after) + ",\"items\":" + Items(p->Loot));
    } else if (name == "QueryGrantedLootForPlayer" && after && function->Outer == UPlayerLootBlueprintLibrary::StaticClass()) {
        const auto* p = static_cast<Params::PlayerLootBlueprintLibrary_QueryGrantedLootForPlayer*>(parameters);
        if (Players.count(p->Player)) Log("\"event\":\"granted_loot_query_callback\",\"items\":" + Items(p->Loot));
    }
}
// No detour or Install function is provided. Root dispatches observations from
// its single existing ProcessEvent hook; this header never grants or breaks.
}
