// QA engine probe, client side (W2): in a client that `vibeless probe --client` starts for a qa-*
// profile, at the title screen (no login, no services). It loads every package object, spawns the
// creature and its stock control locally in front of a probe camera (no AI), draws and samples them,
// forces every package texture fully resident through the texture streamer (the generation-8 crash
// path), then binds both to the stock HUD health widget, reveals them and records the portrait (the
// generation-11/12 empty portrait): what the HUD shows, or, where the HUD does not bind outside a
// hunt, native code's data path from the live actor's identity key to its BehemothInfo row's Icon.
// reports/qa-probe-<id>-client.json is rewritten around every stage, so a crash leaves the stage in
// progress. Rules: QaProbePolicy.h. AGPL-3.0-only.
#pragma once
#include "QaProbe.h"
#include "SDK/UMG_classes.hpp"
#include "SDK/UMG_parameters.hpp"

namespace QaProbe {
enum class ClientPhase { Start, World, Draw, Stream, Portrait, Idle };
inline ClientPhase Client = ClientPhase::Start;
inline UWorld* ClientWorld = nullptr;
inline ULONGLONG WorldSince = 0, NextPoll = 0;
inline long long LastFrames = 0;
inline std::vector<std::pair<UTexture2D*, int32>> Textures;  // parallel to Report.Textures
inline QaProbePolicy::StreamSettle Settle;
inline UHUDBehemothHealthManagerWidget* Hud = nullptr;
inline int32 HudIndex = -1;
// The stock HUD layer that hosts the three behemoth health slots in a hunt.
inline constexpr wchar_t HudClass[] = L"/Game/UI/HUD/Behemoth/behemoth_health_manager_bpw.behemoth_health_manager_bpw_C";
inline constexpr ULONGLONG WorldSettleMs = 5000, WorldTimeoutMs = 180000, PortraitWaitMs = 6000;

inline bool Alive(UObject* object, int32 index) { return object && index >= 0 && UObject::GObjects->GetByIndex(index) == object; }

// A local actor of `type` (no possession, no AI controller); movement off so it stays in view.
inline AActor* SpawnLocal(UWorld* world, UClass* type, const FVector& at, const FRotator& facing, bool creature) {
    const auto transform = UKismetMathLibrary::MakeTransform(at, facing, FVector{1, 1, 1});
    auto* actor = UGameplayStatics::BeginDeferredActorSpawnFromClass(world, type, transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr);
    if (!actor) return nullptr;
    if (creature) {
        auto* pawn = static_cast<APawn*>(actor);
        pawn->AutoPossessAI = EAutoPossessAI::Disabled;
        pawn->AIControllerClass = nullptr;
    }
    actor = UGameplayStatics::FinishSpawningActor(actor, transform);
    if (!actor || !creature) return actor;
    auto* character = static_cast<ACharacter*>(actor);
    if (character->CharacterMovement) character->CharacterMovement->DisableMovement();
    if (character->Mesh) character->Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    return actor;
}

// Stages "spawn" (camera, creature, control) after "load" and "class"; false when the probe cannot go on.
inline bool SpawnStage(UWorld* world, const std::vector<std::pair<std::string, UClass*>>& types) {
    PhaseStart = GetTickCount64();
    auto& spawn = Report.Begin("spawn");
    Write();
    auto* player = UGameplayStatics::GetPlayerController(world, 0);
    auto* camera = UGameplayStatics::GetPlayerCameraManager(world, 0);
    if (!player || !camera) { Report.Fail(spawn, "no local player camera"); return false; }
    const auto eye = camera->GetCameraLocation();
    FRotator flat = camera->GetCameraRotation();
    flat.Pitch = 0; flat.Roll = 0;
    const auto forward = UKismetMathLibrary::GetForwardVector(flat), right = UKismetMathLibrary::GetRightVector(flat);
    const FVector target{eye.X + forward.X * 3000, eye.Y + forward.Y * 3000, eye.Z - 300};
    FRotator facing = flat;
    facing.Yaw += 180;
    // The probe camera frames both subjects side by side (creature left, control right).
    const FVector view{eye.X, eye.Y, eye.Z + 200};
    if (auto* eyeActor = SpawnLocal(world, ACameraActor::StaticClass(), view, UKismetMathLibrary::FindLookAtRotation(view, target), false))
        player->SetViewTargetWithBlend(eyeActor, 0, EViewTargetBlendFunction::VTBlend_Linear, 0, false);
    Actors.assign(Report.Subjects.size(), {});
    for (size_t i = 0; i < types.size(); ++i) {
        const float side = types[i].first == "creature" ? -1100.f : 1100.f;
        const FVector at{target.X + right.X * side, target.Y + right.Y * side, target.Z};
        auto* monster = static_cast<AArchonBehemoth*>(SpawnLocal(world, types[i].second, at, facing, true));
        if (!monster) { Report.Fail(spawn, types[i].first + ": the local spawn returned nothing"); break; }
        Actors[i].Actor = monster;
        Actors[i].Origin = monster->K2_GetActorLocation();
        Report.Subjects[i].Spawned = true;
        Report.Subjects[i].Key = monster->BehemothFullName.ToString();
        if (auto* mesh = monster->Mesh) {
            const int count = (std::min)(mesh->GetNumBones(), QaProbePolicy::MaxBones + 1);
            for (int b = 1; b < count; ++b) Actors[i].Bones.push_back(mesh->GetBoneName(b));
        }
    }
    spawn.Seconds = Seconds(PhaseStart);
    if (spawn.Status == "running") spawn.Status = "ok";
    return spawn.Status == "ok";
}

// Every streamable package asset forced fully resident; package textures are tracked.
inline void ForceResident() {
    for (const auto& [object, index] : Loaded) {
        if (!Alive(object, index)) continue;
        if (object->IsA(UStreamableRenderAsset::StaticClass())) {
            auto* asset = static_cast<UStreamableRenderAsset*>(object);
            asset->bForceMiplevelsToBeResident = 1;
            asset->bGlobalForceMipLevelsToBeResident = 1;
        }
        if (object->IsA(UMaterialInterface::StaticClass()))
            static_cast<UMaterialInterface*>(object)->SetForceMipLevelsToBeResident(true, true, 120.f, 0, true);
        if (object->IsA(UTexture2D::StaticClass()) && Report.Textures.size() < static_cast<size_t>(QaProbePolicy::MaxTextures)) {
            auto* texture = static_cast<UTexture2D*>(object);
            QaProbePolicy::TextureState state;
            state.Path = FeatureSupport::ObjectPath(texture);
            state.SizeX = texture->Blueprint_GetSizeX(); state.SizeY = texture->Blueprint_GetSizeY();
            state.Bias = texture->CachedCombinedLODBias;
            state.Initial = state.Resident = texture->CachedNumResidentLODs;
            state.Streamable = texture->bIsStreamable; state.NeverStream = texture->NeverStream;
            state.Pending = texture->bHasStreamingUpdatePending;
            Report.Textures.push_back(state);
            Textures.push_back({texture, index});
        }
    }
    // Stock textures the subjects use (the streamer's own per-actor request).
    for (const auto& live : Actors) if (live.Actor) live.Actor->PrestreamTextures(120.f, true, 0);
}

// Stage "portrait": the stock HUD health widget, as a hunt shows it, bound to both subjects.
inline bool BindHud(UWorld* world, std::string& error) {
    auto* player = UGameplayStatics::GetPlayerController(world, 0);
    auto* type = UKismetSystemLibrary::LoadClassAsset_Blocking(UKismetSystemLibrary::Conv_SoftClassPathToSoftClassRef(
        UKismetSystemLibrary::MakeSoftClassPath(FString(HudClass))));
    if (!player || !type) { error = type ? "no local player" : "the stock HUD health widget did not load"; return false; }
    auto* widget = UWidgetBlueprintLibrary::Create(world, type, player);
    if (!widget || !widget->IsA(UHUDBehemothHealthManagerWidget::StaticClass())) { error = "the stock HUD health widget could not be created"; return false; }
    Hud = static_cast<UHUDBehemothHealthManagerWidget*>(widget);
    HudIndex = Hud->Index;
    Hud->AddToViewport(10);
    // Control first: it takes the first slot, as the stock creature did in its own hunts.
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < Actors.size(); ++i)
            if (Actors[i].Actor && (Report.Subjects[i].Role == "control") == (pass == 0)) Hud->OnBehemothSpawned(Actors[i].Actor);
    // The portrait appears on reveal (OnRep_ImageRevealed, the client's replication notify).
    for (auto& live : Actors) if (live.Actor) { live.Actor->bImageRevealed = true; live.Actor->OnRep_ImageRevealed(); }
    return true;
}
// Native code's portrait data path (D33): the actor's identity key names a row of the game instance's
// BehemothInfo table, whose Icon is the portrait. False when the table has no such row.
inline bool AssetRowIcon(UWorld* world, const std::string& key, QaProbePolicy::Portrait& portrait) {
    auto* instance = world ? UGameplayStatics::GetGameInstance(world) : nullptr;
    if (key.empty() || !instance || !instance->IsA(UArchonGameInstance::StaticClass())) return false;
    auto* table = static_cast<UArchonGameInstance*>(instance)->BehemothData;
    if (!table || !table->RowStruct || table->RowStruct->GetName() != "BehemothInfo"
        || table->RowMap.NumAllocated() < 0 || table->RowMap.NumAllocated() > 1024) return false;
    const auto lower = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    for (int i = 0; i < table->RowMap.NumAllocated(); ++i) {
        if (!table->RowMap.IsValidIndex(i) || lower(table->RowMap[i].Key().ToString()) != lower(key)) continue;  // FNames compare case-insensitively
        auto* row = reinterpret_cast<FBehemothInfo*>(table->RowMap[i].Value());
        if (!row) return false;
        portrait.Source = "asset-row";
        const auto path = row->Icon.ObjectID.AssetPathName.GetRawString();
        if (path.empty()) return true;
        auto* icon = UKismetSystemLibrary::LoadAsset_Blocking(UKismetSystemLibrary::Conv_SoftObjPathToSoftObjRef(
            UKismetSystemLibrary::MakeSoftObjectPath(FString(Wide(path).c_str()))));
        if (!icon || !icon->IsA(UTexture2D::StaticClass())) return true;
        portrait.Texture = FeatureSupport::ObjectPath(icon);
        portrait.Width = static_cast<UTexture2D*>(icon)->Blueprint_GetSizeX(); portrait.Height = static_cast<UTexture2D*>(icon)->Blueprint_GetSizeY();
        return true;
    }
    return false;
}
// Reads each subject's HUD slot; true once every subject shows a texture there. `last`: the HUD
// did not bind, so fall back to the asset-row path.
inline std::string HudSlots;
inline bool ReadHud(UWorld* world, bool last) {
    if (!Alive(Hud, HudIndex)) return false;
    bool complete = true;
    int slots = 0, bound = 0;
    for (auto* slot : {Hud->BehemothStateWidget1, Hud->BehemothStateWidget2, Hud->BehemothStateWidget3}) slots += slot != nullptr;
    for (size_t i = 0; i < Actors.size(); ++i) {
        auto& hud = Report.Subjects[i].Hud;
        hud = {};
        for (auto* slot : {Hud->BehemothStateWidget1, Hud->BehemothStateWidget2, Hud->BehemothStateWidget3}) {
            if (!slot || !Actors[i].Actor || slot->Behemoth.Get() != Actors[i].Actor) continue;
            hud.Bound = true;
            hud.Source = "hud";
            ++bound;
            if (auto* icon = slot->BehemothIcon) {
                hud.Texture = FeatureSupport::ObjectPath(icon->Brush.ResourceObject);
                hud.Width = static_cast<int>(icon->Brush.ImageSize.X); hud.Height = static_cast<int>(icon->Brush.ImageSize.Y);
                hud.Visible = icon->Visibility != ESlateVisibility::Hidden && icon->Visibility != ESlateVisibility::Collapsed && icon->RenderOpacity > 0;
            }
        }
        if (Actors[i].Actor) hud.Revealed = Actors[i].Actor->bImageRevealed;
        if (!hud.Bound && last && Actors[i].Actor) hud.Bound = AssetRowIcon(world, Report.Subjects[i].Key, hud);
        complete = complete && hud.Bound && !hud.Texture.empty();
    }
    HudSlots = "HUD slots " + std::to_string(slots) + ", bound " + std::to_string(bound);
    return complete;
}

inline void Finish() {
    if (Report.Verdict == "running") Report.Verdict = "pass";
    Report.Begin("finish").Status = "ok";
    End();
}

inline void TickClient(UWorld* world) {
    if (Client == ClientPhase::Idle) return;
    const auto now = GetTickCount64();
    if (Client == ClientPhase::Start) {
        std::string error;
        const bool loaded = LoadRequest(error);
        if (!loaded && (FeatureSupport::Root().empty() || error == "not a qa-* profile" || error == "DR_QA_PROBE is not set")) { Client = ClientPhase::Idle; return; }
        const auto id = loaded ? Request.Id : std::string("invalid");
        ReportPath = std::filesystem::path(FeatureSupport::Root()) / L"reports" / (L"qa-probe-" + Wide(id) + L"-client.json");
        std::error_code ignored;
        std::filesystem::create_directories(ReportPath.parent_path(), ignored);
        Report.Id = id; Report.Side = "client"; Report.Pid = GetCurrentProcessId(); Report.Runtime = VIBELESS_VERSION;
        if (!loaded) { Report.Fail(Report.Begin("request"), error); End(); return; }
        Report.Creature = Request.Creature; Report.Control = Request.Control;
        Report.AssetsRequested = static_cast<int>(Request.Assets.size());
        Report.Begin("world");
        PhaseStart = now;
        Client = ClientPhase::World;
        Write();
        return;
    }
    if (Client == ClientPhase::World) {
        // The title screen: a world with a local player camera, unchanged for a few seconds.
        const bool ready = world && UGameplayStatics::GetPlayerController(world, 0) && UGameplayStatics::GetPlayerCameraManager(world, 0);
        if (!ready || world != ClientWorld) { ClientWorld = ready ? world : nullptr; WorldSince = now; }
        auto& stage = Report.Stages.back();
        if (!ready || now - WorldSince < WorldSettleMs) {
            if (now - PhaseStart > WorldTimeoutMs) { stage.Seconds = Seconds(PhaseStart); Report.Fail(stage, "no title-screen world with a local player after 180 s"); End(); }
            return;
        }
        stage.Status = "ok"; stage.Seconds = Seconds(PhaseStart);
        Report.Map = stage.Detail = FeatureSupport::ObjectPath(world);
        LoadStage(&Loaded);
        std::vector<std::pair<std::string, UClass*>> types;
        if (!ClassStage(types) || !SpawnStage(world, types)) { End(); return; }
        PhaseStart = now;
        Report.Begin("draw");
        NextSample = now;
        SampleEnd = now + static_cast<ULONGLONG>(Request.SampleSeconds) * 1000;
        Client = ClientPhase::Draw;
        Write();
        return;
    }
    if (Client == ClientPhase::Draw) {
        ++Report.Frames;
        if (now < NextSample) return;
        NextSample = now + 250;
        for (size_t i = 0; i < Actors.size(); ++i) Sample(Report.Subjects[i], Actors[i]);
        if (now < SampleEnd) { if ((now - PhaseStart) % 2000 < 250) Write(); return; }
        auto& stage = Report.Stages.back();
        stage.Seconds = Seconds(PhaseStart);
        stage.Status = "ok";
        for (size_t i = 0; i < Actors.size(); ++i)
            if (!Actors[i].Actor || Actors[i].Actor->IsActorBeingDestroyed()) Report.Fail(stage, Report.Subjects[i].Role + " was destroyed while drawn");
        stage.Detail = std::to_string(Report.Frames) + " frames";
        PhaseStart = now;
        Report.Begin("stream");
        Write();
        ForceResident();
        NextPoll = now;
        Client = ClientPhase::Stream;
        Write();
        return;
    }
    if (Client == ClientPhase::Stream) {
        if (now < NextPoll) return;
        NextPoll = now + 250;
        long long total = 0;
        int pending = 0;
        for (size_t i = 0; i < Textures.size(); ++i) {
            auto& state = Report.Textures[i];
            if (!Alive(Textures[i].first, Textures[i].second)) { state.Pending = false; continue; }
            auto* texture = Textures[i].first;
            state.Resident = texture->CachedNumResidentLODs;
            state.Bias = texture->CachedCombinedLODBias;
            state.Pending = texture->bHasStreamingUpdatePending;
            total += state.Resident;
            pending += state.Pending;
        }
        auto& stage = Report.Stages.back();
        const double elapsed = Seconds(PhaseStart);
        if (!Settle.Observe(total, pending, elapsed)) {
            if (elapsed <= QaProbePolicy::StreamTimeoutSeconds) { if ((now - PhaseStart) % 2000 < 250) Write(); return; }
            Report.Fail(stage, std::to_string(pending) + " of " + std::to_string(Textures.size()) + " package textures still streaming after 90 s");
        }
        stage.Seconds = elapsed;
        if (stage.Status == "running") {
            stage.Status = "ok";
            const auto summary = QaProbePolicy::Summarize(Report.Textures);
            stage.Detail = std::to_string(summary.Textures) + " textures, " + std::to_string(summary.Raised) + " streamed in, "
                + std::to_string(summary.Short) + " below a full chain";
        }
        PhaseStart = now;
        auto& portrait = Report.Begin("portrait");
        Write();
        std::string error;
        if (!BindHud(ClientWorld, error)) { Report.Fail(portrait, error); Finish(); return; }
        NextPoll = now;
        Client = ClientPhase::Portrait;
        Write();
        return;
    }
    if (Client == ClientPhase::Portrait) {
        if (now < NextPoll) return;
        NextPoll = now + 250;
        // Icons may load asynchronously; wait until both show one or the wait ends.
        if (!ReadHud(ClientWorld, false) && now - PhaseStart < PortraitWaitMs) return;
        ReadHud(ClientWorld, true);
        auto& stage = Report.Stages.back();
        stage.Seconds = Seconds(PhaseStart);
        stage.Status = "ok";
        const QaProbePolicy::Portrait* control = nullptr;
        for (const auto& s : Report.Subjects) if (s.Role == "control") control = &s.Hud;
        for (const auto& s : Report.Subjects) {
            if (s.Role != "creature") continue;
            const auto result = QaProbePolicy::Judge(s.Hud, control);
            if (result == QaProbePolicy::PortraitResult::Missing)
                Report.Fail(stage, "creature portrait empty (key " + s.Key + (s.Hud.Bound ? ": row without an icon" : ": no HUD binding and no BehemothInfo row")
                    + ") while the control showed " + control->Texture + " (" + control->Source + ")");
            else if (result == QaProbePolicy::PortraitResult::Inconclusive) stage.Detail = "creature portrait empty; the control showed none either (inconclusive)";
            else stage.Detail = "creature shows " + s.Hud.Texture + " (" + s.Hud.Source + ")";
        }
        stage.Detail += "; " + HudSlots;
        Finish();
    }
}
}
