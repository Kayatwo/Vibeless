// Headless, nonreplicated pose evaluation on an old 1.4.4 mesh. AGPL-3.0-only.
#pragma once
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "DojoProbe.h"

namespace PortPoseProbe {
using namespace SDK;
inline ASkeletalMeshActor* Actor = nullptr;
inline USkeletalMeshComponent* Component = nullptr;
inline UWorld* World = nullptr;
inline int32 ActorIndex = -1, ComponentIndex = -1;
inline ULONGLONG Started = 0;
inline std::filesystem::path Report;
inline int Samples = 0;
inline bool Active = false, AllFinite = true, Moved = false, PositionAdvanced = false;
inline float FirstPosition = 0.0f;
inline std::vector<int32> BoneIndices;
inline std::vector<FTransform> FirstPose;

inline void Log(const std::string& body) {
    if (Report.empty()) return;
    std::ofstream out(Report, std::ios::app);
    out << "{\"pid\":" << GetCurrentProcessId() << ",\"uptimeMs\":" << GetTickCount64() << ',' << body << "}\n";
}
inline bool Alive() {
    return Actor && Component && ActorIndex >= 0 && ComponentIndex >= 0
        && UObject::GObjects->GetByIndex(ActorIndex) == Actor
        && UObject::GObjects->GetByIndex(ComponentIndex) == Component && !Actor->IsActorBeingDestroyed();
}
inline bool Finite(const FTransform& transform) {
    return std::isfinite(transform.Translation.X) && std::isfinite(transform.Translation.Y) && std::isfinite(transform.Translation.Z)
        && std::isfinite(transform.Rotation.X) && std::isfinite(transform.Rotation.Y) && std::isfinite(transform.Rotation.Z) && std::isfinite(transform.Rotation.W)
        && std::isfinite(transform.Scale3D.X) && std::isfinite(transform.Scale3D.Y) && std::isfinite(transform.Scale3D.Z);
}
inline void Cleanup(const std::string& reason) {
    bool requested = false, destroying = false;
    if (Actor && ActorIndex >= 0 && UObject::GObjects->GetByIndex(ActorIndex) == Actor) {
        if (!Actor->IsActorBeingDestroyed()) {
            if (Component && ComponentIndex >= 0 && UObject::GObjects->GetByIndex(ComponentIndex) == Component) Component->Stop();
            Actor->K2_DestroyActor();
            requested = true;
        }
        destroying = Actor->IsActorBeingDestroyed();
    }
    Log("\"event\":\"pose_cleanup\",\"reason\":" + DojoProbe::Json(reason)
        + ",\"destroyRequested\":" + DojoProbe::Bool(requested) + ",\"actorDestroying\":" + DojoProbe::Bool(destroying));
    Active = false;
    Actor = nullptr;
    Component = nullptr;
}
inline bool Begin(UWorld* world, UAnimSequenceBase* animation, const std::filesystem::path& report) {
    if (Active) return false;
    Report = report;
    Samples = 0; AllFinite = true; Moved = false; PositionAdvanced = false;
    BoneIndices.clear(); FirstPose.clear();
    const auto fail = [](const std::string& reason) {
        Log("\"event\":\"pose_begin_failed\",\"reason\":" + DojoProbe::Json(reason));
        Cleanup(reason);
        return false;
    };
    if (!DojoProbe::IsExpectedProcess() || !world || !world->AuthorityGameMode || !world->AuthorityGameMode->HasAuthority()
        || !world->NetDriver || world->NetDriver->ServerConnection) return fail("authoritative_lab_server_required");
    auto* animationClass = UObject::FindClassFast("AnimSequenceBase");
    if (!animation || !animationClass || !animation->IsA(animationClass)) return fail("animation_missing_or_wrong_class");
    if (!std::isfinite(animation->SequenceLength) || animation->SequenceLength <= 0 || animation->SequenceLength > 60) return fail("invalid_sequence_length");
    Log("\"event\":\"pose_preflight\",\"animation\":" + DojoProbe::Json(animation->GetName())
        + ",\"seconds\":" + std::to_string(animation->SequenceLength) + ",\"notifyCount\":" + std::to_string(animation->Notifies.Num()));
    if (animation->Notifies.Num() != 0) return fail("animation_has_notifies_no_shared_asset_mutation_permitted");
    const auto soft = UKismetSystemLibrary::MakeSoftObjectPath(L"/Game/Monsters/mcrollin/mcrollin_frost.mcrollin_frost");
    auto* loaded = UKismetSystemLibrary::LoadAsset_Blocking(UKismetSystemLibrary::Conv_SoftObjPathToSoftObjRef(soft));
    auto* meshClass = UObject::FindClassFast("SkeletalMesh");
    if (!loaded || !meshClass || !loaded->IsA(meshClass)) return fail("old_frost_mesh_load_failed");
    auto* mesh = static_cast<USkeletalMesh*>(loaded);
    if (!mesh->Skeleton || animation->Skeleton != mesh->Skeleton) return fail("old_mesh_and_animation_skeletons_do_not_match");
    FTransform transform{};
    transform.Rotation.W = 1.0f;
    transform.Scale3D = FVector{1,1,1};
    transform.Translation = FVector{0,0,2000};
    Log("\"event\":\"pose_spawn_begin\",\"mesh\":" + DojoProbe::Json(mesh->GetName()));
    Actor = static_cast<ASkeletalMeshActor*>(UGameplayStatics::BeginDeferredActorSpawnFromClass(world,
        ASkeletalMeshActor::StaticClass(), transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr));
    if (!Actor) return fail("deferred_skeletal_actor_spawn_failed");
    ActorIndex = Actor->Index;
    Actor->SetReplicates(false);
    Actor->bReplicateMovement = false;
    Actor->SetActorEnableCollision(false);
    Component = Actor->SkeletalMeshComponent;
    if (!Component) return fail("skeletal_mesh_component_missing");
    ComponentIndex = Component->Index;
    Component->PrimaryComponentTick.bCanEverTick = true;
    Component->PrimaryComponentTick.bStartWithTickEnabled = true;
    Component->PrimaryComponentTick.bAllowTickOnDedicatedServer = true;
    Component->PrimaryComponentTick.TickInterval = 0;
    Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Component->bNoRefreshBonesOnServer = false;
    Component->bNoSkeletonUpdateOnServer = false;
    Component->bEnableUpdateRateOptimizations = false;
    Component->bOnlyAllowAutonomousTickPose = false;
    Component->bNoSkeletonUpdate = false;
    Component->bPauseAnims = false;
    Component->bForceRefpose = false;
    Component->bDisablePostProcessBlueprint = true;
    Component->bDisableClothSimulation = true;
    Component->bEnablePhysicsOnDedicatedServer = false;
    Component->GlobalAnimRateScale = 1.0f;
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetGenerateOverlapEvents(false);
    Component->SetEnableGravity(false);
    Component->SetSkeletalMesh(mesh, true);
    UGameplayStatics::FinishSpawningActor(Actor, transform);
    if (!Alive()) return fail("skeletal_actor_finish_failed");
    Component->SetSimulatePhysics(false);
    Component->SetComponentTickEnabled(true);
    const int32 bones = Component->GetNumBones();
    for (const int32 index : {0,1,20,50}) if (index < bones) BoneIndices.push_back(index);
    if (BoneIndices.size() < 2) return fail("old_mesh_has_insufficient_sample_bones");
    World = world;
    Log("\"event\":\"pose_play_begin\",\"actorIndex\":" + std::to_string(ActorIndex)
        + ",\"boneCount\":" + std::to_string(bones) + ",\"notifiesDisabled\":true,\"replication\":false,\"collision\":false");
    Component->PlayAnimation(animation, false);
    Component->SetPlayRate(1.0f);
    Started = GetTickCount64();
    Active = true;
    Log("\"event\":\"pose_play_started\",\"componentTickEnabled\":" + std::string(DojoProbe::Bool(Component->IsComponentTickEnabled())));
    return true;
}
inline void Tick() {
    if (!Active) return;
    if (!DojoProbe::IsExpectedProcess() || UWorld::GetWorld() != World || !Alive()) {
        Log("\"event\":\"pose_failed\",\"reason\":\"world_or_throwaway_actor_changed\"");
        Cleanup("world_or_throwaway_actor_changed");
        return;
    }
    const auto elapsed = GetTickCount64() - Started;
    if ((Samples == 0 && elapsed >= 500) || (Samples == 1 && elapsed >= 2000)) {
        const float position = Component->GetPosition();
        if (!std::isfinite(position)) AllFinite = false;
        if (Samples == 0) FirstPosition = position;
        else PositionAdvanced = std::isfinite(position) && position > FirstPosition + 0.001f;
        for (size_t index = 0; index < BoneIndices.size(); ++index) {
            const auto name = Component->GetBoneName(BoneIndices[index]);
            const auto pose = Component->GetSocketTransform(name, ERelativeTransformSpace::RTS_Component);
            const bool finite = Finite(pose);
            AllFinite = AllFinite && finite;
            float difference = 0;
            if (Samples == 0) FirstPose.push_back(pose);
            else if (finite && Finite(FirstPose[index])) {
                const auto& first = FirstPose[index];
                difference = std::fabs(pose.Translation.X-first.Translation.X) + std::fabs(pose.Translation.Y-first.Translation.Y)
                    + std::fabs(pose.Translation.Z-first.Translation.Z) + std::fabs(pose.Rotation.X-first.Rotation.X)
                    + std::fabs(pose.Rotation.Y-first.Rotation.Y) + std::fabs(pose.Rotation.Z-first.Rotation.Z)
                    + std::fabs(pose.Rotation.W-first.Rotation.W);
                Moved = Moved || difference > 0.001f;
            }
            Log("\"event\":\"pose_bone_sample\",\"sample\":" + std::to_string(Samples)
                + ",\"elapsedMs\":" + std::to_string(elapsed) + ",\"animationPosition\":" + (std::isfinite(position) ? std::to_string(position) : "null")
                + ",\"boneIndex\":" + std::to_string(BoneIndices[index]) + ",\"bone\":" + DojoProbe::Json(name.ToString())
                + ",\"finite\":" + DojoProbe::Bool(finite) + ",\"differenceFromFirst\":" + std::to_string(difference)
                + ",\"x\":" + (finite ? std::to_string(pose.Translation.X) : "null") + ",\"y\":" + (finite ? std::to_string(pose.Translation.Y) : "null")
                + ",\"z\":" + (finite ? std::to_string(pose.Translation.Z) : "null")
                + ",\"qx\":" + (finite ? std::to_string(pose.Rotation.X) : "null") + ",\"qy\":" + (finite ? std::to_string(pose.Rotation.Y) : "null")
                + ",\"qz\":" + (finite ? std::to_string(pose.Rotation.Z) : "null") + ",\"qw\":" + (finite ? std::to_string(pose.Rotation.W) : "null"));
        }
        ++Samples;
    }
    if (elapsed >= 3000) {
        Log("\"event\":\"pose_result\",\"samples\":" + std::to_string(Samples)
            + ",\"allFinite\":" + DojoProbe::Bool(AllFinite) + ",\"boneMovement\":" + DojoProbe::Bool(Moved)
            + ",\"animationPositionAdvanced\":" + DojoProbe::Bool(PositionAdvanced)
            + ",\"headlessBoneEvaluationObserved\":" + DojoProbe::Bool(Samples == 2 && AllFinite && Moved && PositionAdvanced)
            + ",\"visibleClientPlaybackTested\":false");
        Cleanup("three_second_probe_complete");
    }
}
}
