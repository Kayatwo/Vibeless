// Opt-in asset loading on an empty isolated server; no actors, inventory or gameplay writes.
#pragma once
#include <array>
#include "DraskProbe.h"
#include "EquipmentProbePolicy.h"

namespace EquipmentProbe {
using namespace SDK;
struct Asset { const wchar_t* path; const char* type; bool isClass; };
struct StatPair { const char* id; const char* sourceId; bool armor; };
#include "EquipmentProbeAssets.h"
inline bool Activated = false, Finished = false;
inline DWORD EngineThread = 0;
inline UWorld* ActiveWorld = nullptr;
inline unsigned AssetIndex = 0, Events = 0;
inline unsigned StatIndex = 0;
inline ULONGLONG NextConfigPoll = 0, NextLoad = 0;
inline std::string RunId;

inline void Log(const std::string& body) {
    if (++Events > 1024) return;
    const auto report = std::filesystem::path(DojoProbe::Root) / L"reports"
        / (L"equipment-probe-" + std::to_wstring(GetCurrentProcessId()) + L".jsonl");
    std::ofstream out(report, std::ios::app);
    out << "{\"version\":1,\"pid\":" << GetCurrentProcessId()
        << ",\"utc\":" << DojoProbe::Json(DraskProbe::UtcNow()) << ",\"uptimeMs\":" << GetTickCount64()
        << ",\"readOnly\":true,\"runId\":" << DojoProbe::Json(RunId)
        << ",\"assetSet\":" << DojoProbe::Json(AssetSetSha256) << ',' << body << "}\n";
}
inline bool ReadMarker() {
    const auto marker = std::filesystem::path(DojoProbe::Root) / L"data/equipment-probe.ini";
    std::error_code error;
    const auto bytes = std::filesystem::file_size(marker, error);
    if (error || bytes == 0 || bytes > 1024) return false;
    std::ifstream in(marker, std::ios::binary);
    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(in, line)) {
        line = DojoProbe::Trim(line);
        if (line.empty()) continue;
        const auto split = line.find('=');
        if (split == std::string::npos) return false;
        const auto key = DojoProbe::Trim(line.substr(0, split));
        if (key != "Enabled" && key != "RunId" && key != "AssetSet") return false;
        if (!fields.emplace(key, DojoProbe::Trim(line.substr(split + 1))).second) return false;
    }
    if (fields.size() != 3 || fields["Enabled"] != "1"
        || !DojoProbe::ValidId(fields["RunId"]) || fields["AssetSet"] != AssetSetSha256) return false;
    RunId = fields["RunId"];
    return true;
}
#include "EquipmentStatProbe.h"
inline void Tick(UWorld* world, UNetDriver* driver, int port) {
    if (Finished || !world || !driver) return;
    auto* mode = world->AuthorityGameMode;
    const auto mapPath = DojoProbe::ObjectPath(world);
    const auto modeName = mode && mode->Class ? mode->Class->GetName() : std::string();
    const EquipmentProbePolicy::ScopeInput input{
        DojoProbe::IsExpectedProcess(), driver->World == world && world->NetDriver == driver,
        driver->ServerConnection != nullptr, mode != nullptr,
        mode && mode->HasAuthority(), !DojoProbe::FrostHuntEnabled(), port, mapPath, modeName };
    if (!EquipmentProbePolicy::InScope(input)) return;
    if (Activated && (ActiveWorld != world || EngineThread != GetCurrentThreadId()
        || driver->ClientConnections.Num() != 0)) {
        Log("\"event\":\"aborted\",\"reason\":\"world_thread_or_empty_server_guard\"");
        Finished = true;
        return;
    }
    if (!EquipmentProbePolicy::MayLoad(input, driver->ClientConnections.Num())) return;
    const auto now = GetTickCount64();
    if (!Activated) {
        if (now < NextConfigPoll) return;
        NextConfigPoll = now + 1000;
        if (!ReadMarker()) return;
        Activated = true;
        ActiveWorld = world;
        EngineThread = GetCurrentThreadId();
        NextLoad = now + 10000;
        Log("\"event\":\"begin\",\"port\":8766,\"clients\":0,\"map\":"
            + DojoProbe::Json(mapPath) + ",\"assets\":" + std::to_string(Assets.size()));
        return;
    }
    if (now < NextLoad) return;
    NextLoad = now + 500;
    if (AssetIndex >= Assets.size()) {
        if (StatIndex < StatPairs.size()) {
            if (!CheckStatPair(world, StatPairs[StatIndex])) { Finished = true; return; }
            ++StatIndex;
            return;
        }
        Log("\"event\":\"complete\",\"loadedCount\":" + std::to_string(AssetIndex)
            + ",\"statPairs\":" + std::to_string(StatIndex));
        Finished = true;
        return;
    }
    const auto& item = Assets[AssetIndex];
    const std::wstring wide(item.path);
    std::string path;
    for (const auto character : wide) path.push_back(static_cast<char>(character)); // Fixed ASCII paths.
    Log("\"event\":\"load_begin\",\"index\":" + std::to_string(AssetIndex)
        + ",\"path\":" + DojoProbe::Json(path) + ",\"expectedType\":" + DojoProbe::Json(item.type));
    UObject* loaded = nullptr;
    if (item.isClass) {
        const auto soft = UKismetSystemLibrary::MakeSoftClassPath(item.path);
        loaded = UKismetSystemLibrary::LoadClassAsset_Blocking(UKismetSystemLibrary::Conv_SoftClassPathToSoftClassRef(soft));
    } else {
        const auto soft = UKismetSystemLibrary::MakeSoftObjectPath(item.path);
        loaded = UKismetSystemLibrary::LoadAsset_Blocking(UKismetSystemLibrary::Conv_SoftObjPathToSoftObjRef(soft));
    }
    auto* type = UObject::FindClassFast(item.isClass ? "Class" : item.type);
    bool valid = loaded && type && loaded->IsA(type) && DojoProbe::ObjectPath(loaded) == path;
    std::string detail;
    if (valid && item.isClass) {
        auto* klass = static_cast<UClass*>(loaded);
        valid = klass->ClassDefaultObject && klass->SuperStruct;
        detail = ",\"parent\":" + DraskProbe::Object(klass->SuperStruct)
            + ",\"cdo\":" + DraskProbe::Object(klass->ClassDefaultObject);
    } else if (valid && std::string(item.type) == "SkeletalMesh") {
        auto* mesh = static_cast<USkeletalMesh*>(loaded);
        valid = mesh->Skeleton && DraskProbe::Bounded(mesh->Materials, 32) && DraskProbe::Bounded(mesh->LODInfo, 16);
        detail = ",\"skeleton\":" + DraskProbe::Object(mesh->Skeleton)
            + ",\"materials\":" + std::to_string(mesh->Materials.Num())
            + ",\"lods\":" + std::to_string(mesh->LODInfo.Num());
    } else if (valid && std::string(item.type) == "Texture2D") {
        auto* texture = static_cast<UTexture2D*>(loaded);
        const int width = texture->Blueprint_GetSizeX(), height = texture->Blueprint_GetSizeY();
        valid = width > 0 && width <= 8192 && height > 0 && height <= 8192;
        detail = ",\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height);
    }
    Log("\"event\":\"load_result\",\"index\":" + std::to_string(AssetIndex)
        + ",\"path\":" + DraskProbe::Object(loaded) + ",\"loaded\":" + DojoProbe::Bool(loaded != nullptr)
        + ",\"validated\":" + DojoProbe::Bool(valid) + detail);
    if (!valid) { Finished = true; return; }
    ++AssetIndex;
}
}
