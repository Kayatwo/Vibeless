// One-shot asset compatibility checks in the isolated 1.4.4 arena only.
#pragma once
#include "DojoProbe.h"
#include "PortPoseProbe.h"

namespace PortProbe {
using namespace SDK;
inline AActor* AoeProbe = nullptr;
inline int32 AoeProbeIndex = -1;
inline ULONGLONG AoeCleanupAt = 0;
inline void Tick(UWorld* world, UNetDriver* driver, int port) {
    if (port != 8766 || !DojoProbe::IsExpectedProcess() || !world || !driver
        || driver->World != world || driver->ServerConnection) return;
    PortPoseProbe::Tick();
    if (AoeProbe && GetTickCount64() >= AoeCleanupAt) {
        if (AoeProbeIndex >= 0 && UObject::GObjects->GetByIndex(AoeProbeIndex) == AoeProbe
            && !AoeProbe->IsActorBeingDestroyed()) AoeProbe->K2_DestroyActor();
        AoeProbe = nullptr;
    }
    static const ULONGLONG started = GetTickCount64();
    static bool attempted = false;
    if (attempted || GetTickCount64() - started < 10000) return;
    // Explicit file opt-in, bounded identifiers, one attempt per fresh process.
    attempted = true;
    std::ifstream config(std::filesystem::path(DojoProbe::Root) / L"data/port-probe.ini");
    std::string selection;
    if (!std::getline(config, selection)) return;
    selection = DojoProbe::Trim(selection);
    const bool aoe = selection == "ported-frost-aoe";
    if (aoe && (driver->ClientConnections.Num() != 0 || !world->AuthorityGameMode
        || !world->AuthorityGameMode->HasAuthority())) return;
    const bool isClass = selection == "old-frost" || selection == "modern-bellydrift" || selection == "ported-frost" || aoe;
    const bool pose = selection == "adapted-aether-pose";
    if (pose) {
        const auto paks = std::filesystem::path(DojoProbe::Root) / L"game/Archon/Content/Paks";
        if (!std::filesystem::exists(paks / L"Deepfrost_AetherEnter_Adapted_P.pak")
            || std::filesystem::exists(paks / L"Deepfrost_AetherEnter_Probe_P.pak")) return;
    }
    std::string path;
    if (selection == "old-frost") path = "/Game/Monsters/mcrollin/mcbeaver_frost_bp.mcbeaver_frost_bp_C";
    else if (selection == "ported-frost") path = "/Game/Monsters/frost144/mcbeaver_frost_bp.mcbeaver_frost_bp_C";
    else if (aoe) path = "/Game/_shared/blueprints/aoe_base_slam_frost.aoe_base_slam_frost_C";
    else if (selection == "modern-bellydrift") path = "/Game/Monsters/mcrollin/beaver_frost/blueprints/beaver_frost_bellydrift_atk.beaver_frost_bellydrift_atk_C";
    else if (selection == "modern-bellydrift-montage") path = "/Game/Monsters/mcrollin/beaver_frost/montages/b_beaver_frost_bellydrift_atk_mot.b_beaver_frost_bellydrift_atk_mot";
    else if (selection == "modern-aetherenter-animation" || pose) path = "/Game/Monsters/mcrollin/anims/b_tp_beaver_fr_state_aetherenter_fwd_00.b_tp_beaver_fr_state_aetherenter_fwd_00";
    else return;
    const auto report = std::filesystem::path(DojoProbe::Root) / L"reports"
        / (L"port-probe-" + std::to_wstring(GetCurrentProcessId()) + L".jsonl");
    const auto record = [&](const std::string& event, UObject* object) {
        std::ofstream out(report, std::ios::app);
        out << "{\"pid\":" << GetCurrentProcessId() << ",\"uptimeMs\":" << GetTickCount64()
            << ",\"event\":" << DojoProbe::Json(event) << ",\"selection\":" << DojoProbe::Json(selection)
            << ",\"path\":" << DojoProbe::Json(path) << ",\"loaded\":" << DojoProbe::Bool(object != nullptr)
            << ",\"object\":" << (object ? DojoProbe::Json(object->GetName()) : "null")
            << ",\"class\":" << (object && object->Class ? DojoProbe::Json(object->Class->GetName()) : "null");
        auto* animationClass = UObject::FindClassFast("AnimSequenceBase");
        if (object && animationClass && object->IsA(animationClass)) {
            auto* animation = static_cast<UAnimSequenceBase*>(object);
            out << ",\"seconds\":" << animation->SequenceLength << ",\"skeleton\":"
                << (animation->Skeleton ? DojoProbe::Json(animation->Skeleton->GetName()) : "null");
        }
        out << "}\n";
    };
    record("load_begin", nullptr);
    const std::wstring wide(path.begin(), path.end());
    UObject* loaded = nullptr;
    if (isClass) {
        const auto soft = UKismetSystemLibrary::MakeSoftClassPath(wide.c_str());
        loaded = UKismetSystemLibrary::LoadClassAsset_Blocking(UKismetSystemLibrary::Conv_SoftClassPathToSoftClassRef(soft));
    } else {
        const auto soft = UKismetSystemLibrary::MakeSoftObjectPath(wide.c_str());
        loaded = UKismetSystemLibrary::LoadAsset_Blocking(UKismetSystemLibrary::Conv_SoftObjPathToSoftObjRef(soft));
    }
    record("load_result", loaded);
    if (aoe && loaded && loaded->IsA(UClass::StaticClass())
        && static_cast<UClass*>(loaded)->ClassDefaultObject
        && static_cast<UClass*>(loaded)->ClassDefaultObject->IsA(AActor::StaticClass())) {
        FTransform transform{};
        transform.Rotation.W = 1.0f;
        transform.Scale3D = FVector{1,1,1};
        transform.Translation = FVector{0,0,3000};
        record("aoe_spawn_begin", loaded);
        AoeProbe = UGameplayStatics::BeginDeferredActorSpawnFromClass(world, static_cast<UClass*>(loaded), transform,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr);
        if (AoeProbe) {
            AoeProbeIndex = AoeProbe->Index;
            AoeCleanupAt = GetTickCount64() + 15000;
            AoeProbe->SetReplicates(false);
            AoeProbe->SetActorEnableCollision(false);
            UGameplayStatics::FinishSpawningActor(AoeProbe, transform);
        }
        record("aoe_spawn_result", AoeProbe);
    }
    auto* sequenceClass = UObject::FindClassFast("AnimSequenceBase");
    if (pose && loaded && sequenceClass && loaded->IsA(sequenceClass))
        PortPoseProbe::Begin(world, static_cast<UAnimSequenceBase*>(loaded), report);
}
}
