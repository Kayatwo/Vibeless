// Explicit, read-only Thunderdeep preflight in the isolated 1.4.4 arena. AGPL-3.0-only.
#pragma once
#include <cstdio>
#include <map>
#include <set>
#include "DojoProbe.h"
#include "ThunderdeepHuntConfig.h"
#include "SDK/monsterpart_bpc_classes.hpp"

namespace DraskProbe {
using namespace SDK;
inline constexpr char MonsterPath[] = "/Game/Monsters/drask144/fulgar_alpha_bp.fulgar_alpha_bp_C";
inline constexpr unsigned MaxEvents = 2048;
inline DWORD EngineThread = 0;
inline int ActivePort = 0;
inline UWorld* ActiveWorld = nullptr;
inline ULONGLONG NextConfigPoll = 0, NextSample = 0, NextMontagePoll = 0;
inline unsigned EventCount = 0;
inline bool Activated = false, LoadRequested = false, LoadAttempted = false;
inline bool AppearanceAttempted = false;
inline std::string AppearanceSelection;
inline std::string RunId;
inline std::set<int32> ReportedRigs;
inline std::map<int32, std::string> LastMontages;

inline std::string UtcNow() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char result[32]{};
    std::snprintf(result, sizeof(result), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth), static_cast<unsigned>(time.wDay),
        static_cast<unsigned>(time.wHour), static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond),
        static_cast<unsigned>(time.wMilliseconds));
    return result;
}
inline std::string Number(float value) { return std::isfinite(value) ? std::to_string(value) : "null"; }
inline std::string Object(UObject* object) {
    return object ? DojoProbe::Json(DojoProbe::ObjectPath(object).substr(0, 1024)) : "null";
}
template<class T> inline bool Bounded(const TArray<T>& array, int limit) {
    return array.Num() >= 0 && array.Num() <= limit && array.Max() >= array.Num()
        && (array.Num() == 0 || array.IsValidIndex(array.Num() - 1));
}
inline void Log(const std::string& body) {
    if (EventCount >= MaxEvents) return;
    ++EventCount;
    const auto report = std::filesystem::path(DojoProbe::Root) / L"reports"
        / (L"drask-probe-" + std::to_wstring(GetCurrentProcessId()) + L".jsonl");
    std::ofstream out(report, std::ios::app);
    out << "{\"version\":1,\"pid\":" << GetCurrentProcessId() << ",\"utc\":" << DojoProbe::Json(UtcNow())
        << ",\"uptimeMs\":" << GetTickCount64() << ",\"port\":" << ActivePort << ",\"runId\":" << DojoProbe::Json(RunId)
        << ",\"readOnly\":true," << body << "}\n";
}
inline bool Scope(UWorld* world, UNetDriver* driver, int port) {
    if (!DojoProbe::IsExpectedProcess() || !world || !driver
        || driver->World != world || world->NetDriver != driver || driver->ServerConnection
        || !world->AuthorityGameMode || !world->AuthorityGameMode->HasAuthority()) return false;
    if (ThunderdeepHuntConfig::IsDedicatedServer(world, port)) return true;
    if (port != 8766 || world->AuthorityGameMode->Class->GetName() != "BPGM_Archon_Prototype_C"
        || DojoProbe::FrostHuntEnabled()) return false;
    const auto map = world->GetName();
    return map == "arena_ramsgate_00" || map == "dia_moss_triforce";
}
// The marker is read once when valid. It does not queue a spawn or enable a hook.
inline bool ReadMarker() {
    const auto path = std::filesystem::path(DojoProbe::Root) / L"data/drask-probe.ini";
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > 1024) return false;
    std::ifstream input(path, std::ios::binary);
    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(input, line)) {
        if (DojoProbe::Trim(line).empty()) continue;
        const auto split = line.find('=');
        if (split == std::string::npos) return false;
        const auto key = DojoProbe::Trim(line.substr(0, split));
        if (key != "Enabled" && key != "LoadClass" && key != "RunId" && key != "AppearanceProbe") return false;
        if (!fields.emplace(key, DojoProbe::Trim(line.substr(split + 1))).second) return false;
    }
    if (fields["Enabled"] != "1" || (fields["LoadClass"] != "0" && fields["LoadClass"] != "1")
        || !DojoProbe::ValidId(fields["RunId"])) return false;
    if (!fields["AppearanceProbe"].empty() && fields["AppearanceProbe"] != "material"
        && fields["AppearanceProbe"] != "body" && fields["AppearanceProbe"] != "tail"
        && fields["AppearanceProbe"] != "all") return false;
    RunId = fields["RunId"];
    LoadRequested = fields["LoadClass"] == "1";
    AppearanceSelection = fields["AppearanceProbe"];
    return true;
}
// Validate reflected field kind, size and class bounds before reading an old Blueprint member.
inline std::string LootMember(UObject* object, const char* name, bool stringValue = false) {
    if (!object || !object->Class) return "null";
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
            if (!stringValue) return DojoProbe::Json(reinterpret_cast<const FName*>(address)->ToString());
            const auto& text = *reinterpret_cast<const FString*>(address);
            if (!Bounded<wchar_t>(text, 256)) return "null";
            std::string value;
            for (int i = 0; i < text.Num() && text[i]; ++i) value += static_cast<char>(text[i]);
            return DojoProbe::Json(value);
        }
    return "null";
}
inline std::string Materials(USkeletalMeshComponent* mesh) {
    if (!mesh) return "null";
    const int count = mesh->GetNumMaterials();
    if (count < 0 || count > 256) return "null";
    auto* instanceClass = UObject::FindClassFast("MaterialInstance");
    std::string slots = "[";
    for (int i = 0; i < count && i < 32; ++i) {
        if (i) slots += ',';
        auto* material = mesh->GetMaterial(i);
        slots += "{\"slot\":" + std::to_string(i) + ",\"material\":" + Object(material) + ",\"parents\":[";
        auto* current = material;
        for (int depth = 0; depth < 4 && current && instanceClass && current->IsA(instanceClass); ++depth) {
            auto* parent = static_cast<UMaterialInstance*>(current)->Parent;
            if (!parent || parent == current) break;
            if (depth) slots += ',';
            slots += Object(parent);
            current = parent;
        }
        slots += "]}";
    }
    return "{\"slotCount\":" + std::to_string(count) + ",\"truncated\":" + DojoProbe::Bool(count > 32)
        + ",\"slots\":" + slots + "]}";
}
inline std::string Rig(AArchonBehemoth* monster) {
    auto* mesh = monster ? monster->Mesh : nullptr;
    auto* asset = mesh ? mesh->SkeletalMesh : nullptr;
    auto* skeleton = asset ? asset->Skeleton : nullptr;
    const int bones = mesh ? mesh->GetNumBones() : 0;
    std::string names = "[";
    bool first = true;
    for (const int index : {0, 1, 20, 50, 100, 138}) {
        if (!mesh || index >= bones || bones > 4096) continue;
        if (!first) names += ',';
        first = false;
        names += "{\"index\":" + std::to_string(index) + ",\"name\":" + DojoProbe::Json(mesh->GetBoneName(index).ToString()) + "}";
    }
    names += ']';
    UClass* animationClass = mesh ? mesh->AnimClass.Get() : nullptr;
    return "{\"mesh\":" + Object(asset) + ",\"skeleton\":" + Object(skeleton)
        + ",\"animClass\":" + Object(animationClass) + ",\"boneCount\":" + std::to_string(bones)
        + ",\"sampleBoneNames\":" + names + ",\"materials\":" + Materials(mesh) + "}";
}
inline void Preflight(UWorld* world, UNetDriver* driver) {
    // Explicit fixed-asset load gates, only in an empty arena; no world mutation.
    if (!AppearanceSelection.empty() && !AppearanceAttempted) {
        AppearanceAttempted = true;
        if (driver->ClientConnections.Num() != 0) {
            Log("\"event\":\"appearance_skipped_clients_present\"");
        } else {
            struct AppearanceAsset { const char* id; const wchar_t* path; const char* type; };
            const AppearanceAsset assets[] = {
                { "material", L"/Game/Monsters/drask144/materials/fulgar_00_body_m.fulgar_00_body_m", "Material" },
                { "body", L"/Game/Monsters/drask144/thunderdeep/fg_thunderdeep.fg_thunderdeep", "SkeletalMesh" },
                { "tail", L"/Game/Monsters/drask144/thunderdeep/fg_thunderdeep_tail.fg_thunderdeep_tail", "SkeletalMesh" }
            };
            for (const auto& item : assets) {
                if (AppearanceSelection != "all" && AppearanceSelection != item.id) continue;
                const std::wstring wide(item.path);
                std::string path;
                path.reserve(wide.size());
                for (const auto character : wide) path.push_back(static_cast<char>(character)); // Fixed ASCII asset paths above.
                Log("\"event\":" + DojoProbe::Json(std::string("appearance_") + item.id + "_load_begin")
                    + ",\"path\":" + DojoProbe::Json(path));
                const auto soft = UKismetSystemLibrary::MakeSoftObjectPath(item.path);
                auto* asset = UKismetSystemLibrary::LoadAsset_Blocking(UKismetSystemLibrary::Conv_SoftObjPathToSoftObjRef(soft));
                auto* type = UObject::FindClassFast(item.type);
                const bool valid = asset && type && asset->IsA(type) && DojoProbe::ObjectPath(asset) == path;
                auto body = "\"event\":" + DojoProbe::Json(std::string("appearance_") + item.id + "_load_result")
                    + ",\"loaded\":" + DojoProbe::Bool(asset != nullptr) + ",\"validatedType\":" + DojoProbe::Bool(valid)
                    + ",\"path\":" + Object(asset) + ",\"class\":" + Object(asset ? asset->Class : nullptr);
                if (valid && std::string(item.type) == "SkeletalMesh") {
                    auto* mesh = static_cast<USkeletalMesh*>(asset);
                    body += ",\"skeleton\":" + Object(mesh->Skeleton)
                        + ",\"materialSlots\":" + (Bounded(mesh->Materials, 32) ? std::to_string(mesh->Materials.Num()) : "null")
                        + ",\"lodInfoCount\":" + (Bounded(mesh->LODInfo, 16) ? std::to_string(mesh->LODInfo.Num()) : "null");
                }
                Log(body);
                if (!valid) break;
            }
        }
    }
    if (!LoadRequested || LoadAttempted) return;
    LoadAttempted = true;
    if (driver->ClientConnections.Num() != 0) {
        Log("\"event\":\"load_skipped_clients_present\",\"classPath\":" + DojoProbe::Json(MonsterPath));
        return;
    }
    Log("\"event\":\"load_begin\",\"classPath\":" + DojoProbe::Json(MonsterPath));
    const std::wstring path(MonsterPath, MonsterPath + sizeof(MonsterPath) - 1);
    const auto soft = UKismetSystemLibrary::MakeSoftClassPath(path.c_str());
    auto* loaded = UKismetSystemLibrary::LoadClassAsset_Blocking(UKismetSystemLibrary::Conv_SoftClassPathToSoftClassRef(soft));
    auto* behemothClass = UObject::FindClassFast("ArchonBehemoth");
    auto* cdo = loaded ? loaded->ClassDefaultObject : nullptr;
    const bool valid = loaded && DojoProbe::ObjectPath(loaded) == MonsterPath
        && behemothClass && cdo && cdo->IsA(behemothClass);
    std::string body = "\"event\":\"load_result\",\"classPath\":" + DojoProbe::Json(MonsterPath)
        + ",\"loaded\":" + DojoProbe::Bool(loaded != nullptr) + ",\"validatedBehemoth\":" + DojoProbe::Bool(valid)
        + ",\"spawnAttempted\":false,\"map\":" + DojoProbe::Json(world->GetName());
    if (valid) body += ",\"rig\":" + Rig(static_cast<AArchonBehemoth*>(cdo))
        + ",\"archetype\":" + LootMember(cdo, "ArchetypeName")
        + ",\"classification\":" + LootMember(cdo, "ClassificationName")
        + ",\"classificationLootOverride\":" + LootMember(cdo, "ClassificationLootNameOverride")
        + ",\"eomLoot\":" + LootMember(cdo, "EoM_LootID", true);
    Log(body);
}
inline std::string Parts(AArchonBehemoth* monster) {
    auto* type = UObject::FindClassFast("monsterpart_bpc_C");
    if (!type) return "null";
    const auto parts = monster->K2_GetComponentsByClass(type);
    if (!Bounded(parts, 32)) return "null";
    std::string result = "[";
    bool first = true;
    for (auto* component : parts) {
        if (!component || component->GetOwner() != monster || !component->IsA(type)) continue;
        auto* part = static_cast<Umonsterpart_bpc_C*>(component);
        if (!first) result += ',';
        first = false;
        result += "{\"index\":" + std::to_string(part->Index) + ",\"name\":" + DojoProbe::Json(part->PartName.ToString())
            + ",\"health\":" + Number(part->GetCurrentHealth()) + ",\"maxHealth\":" + Number(part->GetMaxHealth())
            + ",\"stage\":" + std::to_string(part->CurrentBreakStage) + ",\"severed\":" + DojoProbe::Bool(part->PartSevered)
            + ",\"stageCount\":" + std::to_string(part->PartBreakData.Num()) + ",\"lootIds\":";
        if (!Bounded(part->LootIDs, 16)) result += "null";
        else {
            result += '[';
            for (int i = 0; i < part->LootIDs.Num(); ++i) {
                if (i) result += ',';
                result += DojoProbe::Json(part->LootIDs[i].ToString());
            }
            result += ']';
        }
        result += '}';
    }
    return result + ']';
}
inline bool FieldMatches(UObject* object, const char* name, const char* kind, int offset, int size) {
    if (!object || !object->Class) return false;
    unsigned depth = 0, count = 0;
    for (auto* type = static_cast<UStruct*>(object->Class); type && ++depth <= 32; type = type->SuperStruct)
        for (auto* field = type->ChildProperties; field && ++count <= 2048; field = field->Next) {
            if (field->Name.ToString() != name) continue;
            auto* property = static_cast<FProperty*>(field);
            return field->ClassPrivate && field->ClassPrivate->Name.ToString() == kind
                && property->Offset == offset && property->ElementSize == size && property->ArrayDim == 1
                && offset >= 0 && object->Class->Size >= offset + size && object->Class->Size <= 1024 * 1024;
        }
    return false;
}
inline std::string Tether(UArchonAggroComponent* aggro) {
    if (!aggro) return "null";
    // The old SDK and reflected property descriptors must agree before direct member reads.
    const bool layout = FieldMatches(aggro, "TetherActorPtr", "WeakObjectProperty", 0x130, 8)
        && FieldMatches(aggro, "TetherRadius", "FloatProperty", 0x138, 4);
    auto* actor = aggro->GetTetherActor();
    if (actor && actor->IsActorBeingDestroyed()) actor = nullptr;
    auto* weakActor = layout ? aggro->TetherActorPtr.Get() : nullptr;
    if (weakActor && weakActor->IsActorBeingDestroyed()) weakActor = nullptr;
    std::string result = "{\"reflectedLayoutMatchesOldSdk\":" + std::string(DojoProbe::Bool(layout))
        + ",\"actor\":" + Object(actor) + ",\"actorFromWeakPointer\":" + Object(weakActor)
        + ",\"radius\":" + (layout ? Number(aggro->TetherRadius) : "null");
    if (actor) {
        const auto position = actor->K2_GetActorLocation();
        result += ",\"x\":" + Number(position.X) + ",\"y\":" + Number(position.Y) + ",\"z\":" + Number(position.Z);
    }
    return result + '}';
}
inline void Sample(AArchonBehemoth* monster) {
    const auto position = monster->K2_GetActorLocation();
    auto* target = DojoProbe::ActorOutput(monster, "GetCurrentTarget_interface");
    auto* patrol = DojoProbe::ActorOutput(monster, "GetCurrentPatrolPoint");
    auto* animation = monster->Mesh ? monster->Mesh->GetAnimInstance() : nullptr;
    auto* montage = animation ? animation->GetCurrentActiveMontage() : nullptr;
    auto* type = UObject::FindClassFast("ArchonAggroComponent");
    auto* aggro = type ? static_cast<UArchonAggroComponent*>(monster->GetComponentByClass(type)) : nullptr;
    std::string body = "\"event\":\"monster_sample\",\"monsterIndex\":" + std::to_string(monster->Index)
        + ",\"classPath\":" + Object(monster->Class) + ",\"health\":" + Number(monster->GetCurrentHealth())
        + ",\"maxHealth\":" + Number(monster->GetMaxHealth()) + ",\"imageRevealed\":" + DojoProbe::Bool(monster->bImageRevealed)
        + ",\"globalPower\":" + Number(monster->GlobalPower)
        + ",\"globalResistance\":" + Number(monster->GlobalResistance)
        + ",\"x\":" + Number(position.X) + ",\"y\":" + Number(position.Y) + ",\"z\":" + Number(position.Z)
        + ",\"controller\":" + Object(monster->Controller) + ",\"currentTarget\":" + Object(target)
        + ",\"patrolPoint\":" + Object(patrol) + ",\"activeMontage\":" + Object(montage)
        + ",\"aggroTargets\":" + (aggro ? std::to_string(aggro->NumValidTargets) : "null")
        + ",\"tether\":" + Tether(aggro)
        + ",\"parts\":" + Parts(monster);
    if (ReportedRigs.insert(monster->Index).second) body += ",\"rig\":" + Rig(monster)
        + ",\"archetype\":" + LootMember(monster, "ArchetypeName")
        + ",\"classification\":" + LootMember(monster, "ClassificationName")
        + ",\"classificationLootOverride\":" + LootMember(monster, "ClassificationLootNameOverride")
        + ",\"eomLoot\":" + LootMember(monster, "EoM_LootID", true);
    Log(body);
}
// Capture short attacks without increasing the expensive part/rig sample rate.
inline void ObserveMontage(AArchonBehemoth* monster) {
    auto* animation = monster->Mesh ? monster->Mesh->GetAnimInstance() : nullptr;
    auto* montage = animation ? animation->GetCurrentActiveMontage() : nullptr;
    const auto path = montage ? DojoProbe::ObjectPath(montage) : std::string{};
    const auto found = LastMontages.find(monster->Index);
    if (found != LastMontages.end() && found->second == path) return;
    LastMontages[monster->Index] = path;
    const auto position = monster->K2_GetActorLocation();
    Log("\"event\":\"montage_change\",\"monsterIndex\":" + std::to_string(monster->Index)
        + ",\"classPath\":" + Object(monster->Class) + ",\"activeMontage\":" + Object(montage)
        + ",\"health\":" + Number(monster->GetCurrentHealth())
        + ",\"x\":" + Number(position.X) + ",\"y\":" + Number(position.Y) + ",\"z\":" + Number(position.Z));
}
// Call on the existing server engine thread after networking restores world->NetDriver.
// Loading requires an empty arena. Observation never spawns, attacks, breaks or grants.
inline void Tick(UWorld* world, UNetDriver* driver, int port) {
    if (!Scope(world, driver, port) || EventCount >= MaxEvents) return;
    ActivePort = port;
    if (!EngineThread) EngineThread = GetCurrentThreadId();
    if (EngineThread != GetCurrentThreadId()) return;
    const auto now = GetTickCount64();
    if (!Activated) {
        if (now < NextConfigPoll) return;
        NextConfigPoll = now + 2000;
        if (!ReadMarker()) return;
        Activated = true;
        Log("\"event\":\"enabled\",\"classPath\":" + DojoProbe::Json(MonsterPath)
            + ",\"loadRequested\":" + DojoProbe::Bool(LoadRequested));
    }
    if (ActiveWorld != world) {
        ActiveWorld = world;
        ReportedRigs.clear();
        LastMontages.clear();
        NextSample = now + 10000;
        Log("\"event\":\"world\",\"map\":" + DojoProbe::Json(world->GetName()));
    }
    const bool sampleDue = now >= NextSample;
    const bool montageDue = now >= NextMontagePoll;
    if (!sampleDue && !montageDue) return;
    if (sampleDue) {
        NextSample = now + 2000;
        Preflight(world, driver);
    }
    if (montageDue) NextMontagePoll = now + 100;
    auto* type = UObject::FindClassFast("ArchonBehemoth");
    if (!type) return;
    TArray<AActor*> actors;
    UGameplayStatics::GetAllActorsOfClass(world, type, &actors);
    if (!Bounded(actors, 32)) return;
    unsigned count = 0;
    for (auto* actor : actors) {
        if (!actor || !actor->Class || actor->IsActorBeingDestroyed() || !actor->HasAuthority()
            || DojoProbe::ObjectPath(actor->Class) != MonsterPath || ++count > 3) continue;
        if (montageDue) ObserveMontage(static_cast<AArchonBehemoth*>(actor));
        if (sampleDue) Sample(static_cast<AArchonBehemoth*>(actor));
    }
}
}
