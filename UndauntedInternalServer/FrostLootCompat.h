// Optional old loot-family compatibility for the unique ported Frost actor. AGPL-3.0-only.
#pragma once
#include <algorithm>
#include <cctype>
#include <map>
#include "DojoProbe.h"
#include "FrostHuntConfig.h"

namespace FrostLootCompat {
using namespace SDK;
inline UWorld* LastWorld = nullptr;
inline ULONGLONG NextPoll = 0;
inline std::map<UObject*, int> Processed;
inline bool LastEnabled = false, ConfigReported = false;
inline unsigned EventCount = 0;
inline int ActivePort = 0;
inline std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
inline void Log(const std::string& body) {
    if (++EventCount > 256) return;
    std::ofstream out(std::filesystem::path(DojoProbe::Root()) / L"reports/frost-loot-compat.jsonl", std::ios::app);
    out << "{\"pid\":" << GetCurrentProcessId() << ",\"uptimeMs\":" << GetTickCount64()
        << ",\"port\":" << ActivePort << ',' << body << "}\n";
}
// Deliberately reread on the 500 ms engine-tick schedule. Absent/malformed means off.
inline bool ReadEnabled() {
    const auto path = std::filesystem::path(DojoProbe::Root()) / L"data/frost-loot-compat.ini";
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
}
inline bool AllowedWorld(UWorld* world, UNetDriver* driver, int port) {
    if (!DojoProbe::IsExpectedProcess() || !world || !driver || driver->World != world
        || world->NetDriver != driver || driver->ServerConnection || !world->AuthorityGameMode
        || !world->AuthorityGameMode->HasAuthority() || !world->AuthorityGameMode->Class
        || world->AuthorityGameMode->Class->GetName() != "BPGM_Archon_Prototype_C") return false;
    return FrostHuntConfig::IsDedicatedServer(world, port)
        || (port == 8766 && DojoProbe::FrostHuntEnabled()
            && (world->GetName() == "arena_ramsgate_00" || world->GetName() == "dia_moss_triforce"));
}
inline FName* NameMember(UObject* object, const char* name, int& offset) {
    unsigned depth = 0, fields = 0;
    for (auto* type = static_cast<UStruct*>(object->Class); type && ++depth <= 32; type = type->SuperStruct)
        for (auto* field = type->ChildProperties; field && ++fields <= 2048; field = field->Next) {
            if (field->Name.ToString() != name) continue;
            auto* property = static_cast<FProperty*>(field);
            if (!field->ClassPrivate || field->ClassPrivate->Name.ToString() != "NameProperty"
                || property->ElementSize != sizeof(FName) || property->ArrayDim != 1 || property->Offset < 0
                || object->Class->Size > 1024 * 1024 || property->Offset > object->Class->Size - static_cast<int>(sizeof(FName))) return nullptr;
            offset = property->Offset;
            return reinterpret_cast<FName*>(reinterpret_cast<uint8*>(object) + property->Offset);
        }
    return nullptr;
}
inline std::string ResolvedLootName(UObject* object) {
    auto* function = DojoProbe::FindFunction(object, "GetBehemothLootName");
    if (!function) return "null";
    unsigned parameters = 0, fields = 0;
    bool valid = true;
    for (auto* field = function->ChildProperties; field && ++fields <= 2048; field = field->Next) {
        auto* property = static_cast<FProperty*>(field);
        if (!(property->PropertyFlags & static_cast<uint64>(EPropertyFlags::Parm))) continue;
        ++parameters;
        const auto name = field->Name.ToString();
        const int expectedOffset = name == "ArchetypeName" ? 0 : name == "ClassificationName" ? sizeof(FName) : -1;
        valid = valid && expectedOffset >= 0 && field->ClassPrivate && field->ClassPrivate->Name.ToString() == "NameProperty"
            && property->ElementSize == sizeof(FName) && property->ArrayDim == 1 && property->Offset == expectedOffset
            && (property->PropertyFlags & static_cast<uint64>(EPropertyFlags::OutParm));
    }
    if (!valid || parameters != 2 || fields > 2048) return "null";
    struct { FName ArchetypeName, ClassificationName; } result{};
    object->ProcessEvent(function, &result);
    return "{\"archetype\":" + DojoProbe::Json(result.ArchetypeName.ToString())
        + ",\"classification\":" + DojoProbe::Json(result.ClassificationName.ToString()) + "}";
}
// Root calls BEFORE FrostRewardProbe::TickServer on the existing engine thread.
// No CDO, combat classification, loot table, quantity, health, damage or grant is changed.
// Turning the marker off prevents future edits; it does not rewrite an already adapted actor.
inline void TickServer(UWorld* world, UNetDriver* driver, int port) {
    if (!DojoProbe::IsExpectedProcess()) return;
    const auto now = GetTickCount64();
    if (now < NextPoll) return;
    NextPoll = now + 500;
    ActivePort = port;
    const bool enabled = ReadEnabled();
    if (!ConfigReported || enabled != LastEnabled) {
        ConfigReported = true; LastEnabled = enabled;
        Log("\"event\":\"configuration\",\"enabled\":" + std::string(DojoProbe::Bool(enabled)));
    }
    if (LastWorld != world) { LastWorld = world; Processed.clear(); }
    if (!enabled || !AllowedWorld(world, driver, port)) return;
    TArray<AActor*> actors;
    UGameplayStatics::GetAllActorsOfClass(world, AArchonBehemoth::StaticClass(), &actors);
    if (actors.Num() < 0 || actors.Num() > 32 || (actors.Num() && !actors.IsValid())) return;
    unsigned count = 0;
    for (auto* actor : actors) {
        if (!actor || !actor->Class || actor->IsActorBeingDestroyed() || !actor->HasAuthority()
            || DojoProbe::ObjectPath(actor->Class) != FrostHuntConfig::MonsterPath || ++count > 4) continue;
        const auto found = Processed.find(actor);
        if (found != Processed.end() && found->second == actor->Index) continue;
        Processed[actor] = actor->Index;
        int overrideOffset = -1, classificationOffset = -1;
        auto* classification = NameMember(actor, "ClassificationName", classificationOffset);
        auto* overrideName = NameMember(actor, "ClassificationLootNameOverride", overrideOffset);
        if (!classification || !overrideName || overrideOffset == classificationOffset || Lower(classification->ToString()) != "frost") {
            Log("\"event\":\"rejected\",\"actorIndex\":" + std::to_string(actor->Index)
                + ",\"reason\":\"required_frost_name_fields_not_valid\"");
            continue;
        }
        const auto before = overrideName->ToString(), actualClassification = classification->ToString();
        const auto normalizedBefore = Lower(before);
        if (normalizedBefore != "none" && normalizedBefore != "frost" && normalizedBefore != "normal") {
            Log("\"event\":\"rejected\",\"actorIndex\":" + std::to_string(actor->Index)
                + ",\"reason\":\"unexpected_existing_loot_override\",\"before\":" + DojoProbe::Json(before));
            continue;
        }
        const auto resolvedBefore = ResolvedLootName(actor);
        const auto normal = UKismetStringLibrary::Conv_StringToName(FString(L"Normal"));
        if (Lower(normal.ToString()) != "normal") continue;
        const bool edited = normalizedBefore != "normal";
        if (edited) *overrideName = normal;
        const auto resolvedAfter = ResolvedLootName(actor);
        Log("\"event\":\"actor_loot_override\",\"actorIndex\":" + std::to_string(actor->Index)
            + ",\"edited\":" + DojoProbe::Bool(edited) + ",\"fieldOffset\":" + std::to_string(overrideOffset)
            + ",\"before\":" + DojoProbe::Json(before) + ",\"after\":" + DojoProbe::Json(overrideName->ToString())
            + ",\"combatClassificationBefore\":" + DojoProbe::Json(actualClassification)
            + ",\"combatClassificationAfter\":" + DojoProbe::Json(classification->ToString())
            + ",\"resolvedBefore\":" + resolvedBefore + ",\"resolvedAfter\":" + resolvedAfter);
    }
}
}
