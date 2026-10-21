// QA engine probe (W2): on a dedicated arena server that `vibeless probe` starts for a qa-* profile,
// load every object of a creature package, spawn the creature (and an optional stock control) and
// sample their poses. reports/qa-probe-<id>.json is rewritten before and after every stage, so a
// crash or stall leaves the stage and asset in progress; the launcher adds exit code and crash
// evidence. Rules and report format: QaProbePolicy.h. AGPL-3.0-only.
#pragma once
#include "QaProbePolicy.h"
#include "FeatureSupport.h"
#include "HuntWorld.h"
#include "VibelessVersion.h"
#include "SDK/Engine_parameters.hpp"
#include "SDK/bp_gamemode_behemoth_spawner_component_parameters.hpp"
#include "SDK/bp_gamemode_behemoth_spawner_component_classes.hpp"
#include "SDK/NavigationSystem_classes.hpp"

namespace QaProbe {
using namespace SDK;
enum class Phase { Start, World, Load, Spawn, Sample, Finish, Idle };
inline Phase Current = Phase::Start;
inline QaProbePolicy::Request Request;
inline QaProbePolicy::Report Report;
inline std::filesystem::path ReportPath;
inline ULONGLONG PhaseStart = 0, NextSample = 0, SampleEnd = 0;
struct Live { AArchonBehemoth* Actor = nullptr; FVector Origin{}; std::vector<FName> Bones; };
inline std::vector<Live> Actors;  // parallel to Report.Subjects

inline std::string Narrow(const std::wstring& text) {
    std::string out;
    for (const wchar_t c : text) out += c < 128 ? static_cast<char>(c) : '?';
    return out;
}
inline std::wstring Wide(const std::string& text) { return std::wstring(text.begin(), text.end()); }
inline double Seconds(ULONGLONG since) { return (GetTickCount64() - since) / 1000.0; }

// Atomic replace, so the launcher never reads half a report.
inline void Write() {
    if (ReportPath.empty()) return;
    const auto temporary = std::filesystem::path(ReportPath.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    { std::ofstream out(temporary, std::ios::binary | std::ios::trunc); out << Report.ToJson(); }
    std::error_code ec;
    if (!MoveFileExW(temporary.c_str(), ReportPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) std::filesystem::remove(temporary, ec);
}
// Ends the probe server the way the idle timeout does: no CRT teardown in a running UE process.
inline void End() {
    Report.Finished = true;
    Write();
    TerminateProcess(GetCurrentProcess(), 0);
}

// DR_QA_PROBE must name a file in <profile>/data/qa-probe/ of a qa-* profile.
inline bool LoadRequest(std::string& error) {
    const auto& root = FeatureSupport::Root();
    if (root.empty() || !QaProbePolicy::IsQaProfile(root)) { error = "not a qa-* profile"; return false; }
    wchar_t value[32768] = {};
    const DWORD length = GetEnvironmentVariableW(L"DR_QA_PROBE", value, 32768);
    if (length == 0 || length >= 32768) { error = "DR_QA_PROBE is not set"; return false; }
    std::error_code ec;
    const auto file = std::filesystem::weakly_canonical(std::filesystem::path(std::wstring(value, length)), ec);
    const auto folder = std::filesystem::weakly_canonical(std::filesystem::path(root) / L"data" / L"qa-probe", ec);
    auto lower = [](std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(towlower(c)); return s; };
    if (ec || lower(file.parent_path().wstring()) != lower(folder.wstring())) { error = "request is outside data/qa-probe"; return false; }
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size > QaProbePolicy::MaxRequestBytes) { error = "request missing or too large"; return false; }
    std::ifstream input(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto parsed = QaProbePolicy::Parse(text, error);
    if (!parsed) return false;
    Request = std::move(*parsed);
    return true;
}

inline UClass* LoadClass(const std::string& path) {
    const auto wide = Wide(path);
    return UKismetSystemLibrary::LoadClassAsset_Blocking(UKismetSystemLibrary::Conv_SoftClassPathToSoftClassRef(
        UKismetSystemLibrary::MakeSoftClassPath(FString(wide.c_str()))));
}
inline bool IsBehemoth(UClass* type) {
    for (auto* parent = static_cast<UStruct*>(type); parent; parent = parent->SuperStruct)
        if (parent == AArchonBehemoth::StaticClass()) return true;
    return false;
}

// What the spawn-point search saw, for the report when it finds nothing.
struct Search { int Anchors = 0, Starts = 0, Volumes = 0, Tries = 0, Traces = 0, Projections = 0, Random = 0; };
// A walkable, navigable point near desired (as Test Arena's spawn does in front of the player).
inline bool Ground(UWorld* world, const FVector& desired, FVector& location, Search& seen) {
    TArray<AActor*> ignore;
    FHitResult hit{};
    ++seen.Tries;
    const bool ground = UKismetSystemLibrary::LineTraceSingle(world, {desired.X, desired.Y, desired.Z + 800}, {desired.X, desired.Y, desired.Z - 3000},
        ETraceTypeQuery::TraceTypeQuery1, false, ignore, EDrawDebugTrace::None, &hit, true, {}, {}, 0)
        && hit.bBlockingHit && !hit.bStartPenetrating && hit.ImpactNormal.Z >= 0.55f;
    if (!ground) return false;
    ++seen.Traces;
    if (!UNavigationSystemV1::K2_ProjectPointToNavigation(world, hit.ImpactPoint, &location, nullptr, {}, FVector{500, 500, 800})) return false;
    ++seen.Projections;
    return true;
}
// Anchors: player starts, then navigation bounds volumes, then the world origin. Around each, fixed
// offsets and random reachable navigation points. `avoid` keeps the control away from the creature.
inline bool SpawnPoint(UWorld* world, const FVector* avoid, FVector& location, Search& seen) {
    std::vector<FVector> anchors;
    for (auto* type : {APlayerStart::StaticClass(), ANavMeshBoundsVolume::StaticClass()}) {
        TArray<AActor*> actors;
        UGameplayStatics::GetAllActorsOfClass(world, type, &actors);
        if (actors.Num() < 0 || actors.Num() > 256) continue;
        for (auto* actor : actors) if (actor) anchors.push_back(actor->K2_GetActorLocation());
        (type == APlayerStart::StaticClass() ? seen.Starts : seen.Volumes) = actors.Num();
    }
    anchors.push_back({0, 0, 0});
    seen.Anchors = static_cast<int>(anchors.size());
    const auto clear = [avoid](const FVector& at) {
        if (!avoid) return true;
        const double dx = at.X - avoid->X, dy = at.Y - avoid->Y;
        return dx * dx + dy * dy >= 2500.0 * 2500.0;
    };
    static constexpr float Offsets[][2] = {{1600, 0}, {0, 1600}, {-1600, 0}, {0, -1600}, {3200, 0}, {0, 3200}, {-3200, 0}, {0, -3200}};
    for (const auto& origin : anchors) {
        for (const auto& offset : Offsets)
            if (Ground(world, {origin.X + offset[0], origin.Y + offset[1], origin.Z}, location, seen) && clear(location)) return true;
        for (int i = 0; i < 8; ++i) {
            FVector random{};
            if (!UNavigationSystemV1::K2_GetRandomReachablePointInRadius(world, origin, &random, 5000, nullptr, {})) continue;
            ++seen.Random;
            if (Ground(world, random, location, seen) && clear(location)) return true;
        }
    }
    return false;
}
inline std::string Describe(const Search& s) {
    return "player starts " + std::to_string(s.Starts) + ", nav volumes " + std::to_string(s.Volumes) + ", anchors " + std::to_string(s.Anchors)
        + ", random nav points " + std::to_string(s.Random) + ", points tried " + std::to_string(s.Tries) + ", ground hits " + std::to_string(s.Traces)
        + ", navigable " + std::to_string(s.Projections);
}
// The arena spawner component, as Test Arena uses it, without intros or mission end.
inline AArchonBehemoth* Spawn(UWorld* world, UClass* type, const FVector& location, std::string& error) {
    auto* mode = HuntWorld::IslandMode(world);
    auto* state = HuntWorld::State(world);
    auto* component = mode ? mode->GetComponentByClass(Ubp_gamemode_behemoth_spawner_component_C::StaticClass()) : nullptr;
    auto* spawn = FeatureSupport::FindFunction(component, "SpawnBehemothAtLocation");
    if (!component || !spawn) { error = "no behemoth spawner component"; return nullptr; }
    auto* spawner = static_cast<Ubp_gamemode_behemoth_spawner_component_C*>(component);
    spawner->EnableBehemothIntros = false;
    spawner->SpawnedBehemothsShouldEndMission = false;
    Params::bp_gamemode_behemoth_spawner_component_C_SpawnBehemothAtLocation request{};
    request.MonsterClass = type; request.Location = location; request.TryToAlignToGround = true;
    request.SpawnParams.IsTemporarySpawnedBehemoth_5_22372EA1469479FC8833F3A5F783A857 = true;
    request.SpawnParams.DelayAfterDeathBeforeDissolve_12_BF65FBEA4583569FF6942D9D01DC13DD = 5;
    component->ProcessEvent(spawn, &request);
    auto* monster = request.Behemoth;
    if (!monster) { error = "the spawner returned no creature"; return nullptr; }
    if (!HuntWorld::Contains(mode->SpawnedBehemoths, monster))
        if (auto* registration = FeatureSupport::FindFunction(mode, "RegisterBehemothSpawned")) {
            struct { AArchonBehemoth* Monster; } parameters{monster}; mode->ProcessEvent(registration, &parameters);
        }
    if (state && !HuntWorld::Contains(state->SpawnedBehemoths, monster))
        if (auto* tracking = FeatureSupport::FindFunction(state, "TrackBehemothSpawn")) {
            struct { AArchonBehemoth* Monster; } parameters{monster}; state->ProcessEvent(tracking, &parameters);
        }
    return monster;
}

inline void Sample(QaProbePolicy::Subject& subject, Live& live) {
    auto* monster = live.Actor;
    if (!monster || monster->IsActorBeingDestroyed()) return;
    auto* mesh = monster->Mesh;
    if (!mesh) return;
    std::vector<QaProbePolicy::Vec> bones;
    bones.reserve(live.Bones.size());
    for (const auto& bone : live.Bones) {
        const auto t = mesh->GetSocketTransform(bone, ERelativeTransformSpace::RTS_Component);
        bones.push_back({t.Translation.X, t.Translation.Y, t.Translation.Z});
    }
    subject.Track.Add(bones);
    if (auto* anim = mesh->GetAnimInstance()) {
        if (subject.AnimClass.empty()) subject.AnimClass = FeatureSupport::ObjectPath(anim->Class);
        if (auto* montage = anim->GetCurrentActiveMontage()) subject.AddMontage(FeatureSupport::ObjectPath(montage));
    }
    const auto at = monster->K2_GetActorLocation();
    const double dx = at.X - live.Origin.X, dy = at.Y - live.Origin.Y, dz = at.Z - live.Origin.Z;
    subject.MovedCm = (std::max)(subject.MovedCm, std::sqrt(dx * dx + dy * dy + dz * dz));
    subject.Health = monster->GetCurrentHealth();
    subject.MaxHealth = monster->GetMaxHealth();
}

inline void TickServer(UWorld* world, int port) {
    if (Current == Phase::Idle || !world) return;
    if (Current == Phase::Start) {
        std::string error;
        const bool loaded = LoadRequest(error);
        const auto id = loaded ? Request.Id : std::string("invalid");
        ReportPath = std::filesystem::path(FeatureSupport::Root()) / L"reports" / (L"qa-probe-" + Wide(id) + L".json");
        std::error_code ignored;
        std::filesystem::create_directories(ReportPath.parent_path(), ignored);
        Report.Id = id; Report.Port = port; Report.Pid = GetCurrentProcessId(); Report.Runtime = VIBELESS_VERSION;
        Report.Map = FeatureSupport::ObjectPath(world);
        if (!loaded) {
            auto& stage = Report.Begin("request");
            Report.Fail(stage, error);
            if (FeatureSupport::Root().empty() || error == "not a qa-* profile" || error == "DR_QA_PROBE is not set") { Current = Phase::Idle; return; }
            End();
            return;
        }
        Report.Creature = Request.Creature; Report.Control = Request.Control;
        Report.AssetsRequested = static_cast<int>(Request.Assets.size());
        Report.Begin("world");
        PhaseStart = GetTickCount64();
        Current = Phase::World;
        Write();
        return;
    }
    if (Current == Phase::World) {
        auto& stage = Report.Stages.back();
        if (HuntWorld::IslandMode(world) && world->GameState) {
            stage.Status = "ok"; stage.Seconds = Seconds(PhaseStart);
            stage.Detail = FeatureSupport::ObjectPath(world->AuthorityGameMode ? world->AuthorityGameMode->Class : nullptr);
            Current = Phase::Load;
        } else if (Seconds(PhaseStart) > 90) {
            stage.Seconds = Seconds(PhaseStart);
            Report.Fail(stage, "no island game mode after 90 s");
            End();
        }
        return;
    }
    if (Current == Phase::Load) {
        // Blocking loads on the game thread; the report names each object before it loads.
        PhaseStart = GetTickCount64();
        auto& stage = Report.Begin("load");
        for (const auto& asset : Request.Assets) {
            Report.CurrentAsset = asset;
            Write();
            const auto wide = Wide(asset);
            auto* object = UKismetSystemLibrary::LoadAsset_Blocking(UKismetSystemLibrary::Conv_SoftObjPathToSoftObjRef(
                UKismetSystemLibrary::MakeSoftObjectPath(FString(wide.c_str()))));
            // Cooked Blueprint packages keep only their generated class: <package>.<name>_C.
            bool loaded = object != nullptr;
            if (!loaded && LoadClass(asset + "_C")) { loaded = true; ++Report.AssetsAsClass; }
            if (loaded) ++Report.AssetsLoaded; else Report.FailedAssets.push_back(asset);
        }
        Report.CurrentAsset.clear();
        stage.Seconds = Seconds(PhaseStart);
        if (Report.FailedAssets.empty()) stage.Status = "ok";
        else Report.Fail(stage, std::to_string(Report.FailedAssets.size()) + " of " + std::to_string(Report.AssetsRequested) + " objects did not load");

        PhaseStart = GetTickCount64();
        auto& classes = Report.Begin("class");
        Write();
        std::vector<std::pair<std::string, UClass*>> types;
        for (const auto& [role, path] : {std::pair<std::string, std::string>{"creature", Request.Creature}, {"control", Request.Control}}) {
            if (path.empty()) continue;
            auto* type = LoadClass(path);
            QaProbePolicy::Subject subject; subject.Role = role; subject.Class = path;
            Report.Subjects.push_back(subject);
            types.push_back({role, type});
            if (!type || !IsBehemoth(type)) Report.Fail(classes, role + " class " + (type ? "is not a behemoth" : "did not load"));
        }
        classes.Seconds = Seconds(PhaseStart);
        if (classes.Status == "running") classes.Status = "ok";
        if (classes.Status != "ok") { End(); return; }

        // Spawn: a crash or overflow here leaves "spawn" running in the report.
        PhaseStart = GetTickCount64();
        auto& spawn = Report.Begin("spawn");
        Write();
        Actors.assign(Report.Subjects.size(), {});
        const FVector* first = nullptr;
        for (size_t i = 0; i < types.size(); ++i) {
            FVector location{};
            Search seen;
            if (!SpawnPoint(world, first, location, seen)) { Report.Fail(spawn, "no navigable spawn point for the " + types[i].first + " (" + Describe(seen) + ")"); break; }
            std::string error;
            auto* monster = Spawn(world, types[i].second, location, error);
            if (!monster) { Report.Fail(spawn, types[i].first + ": " + error); break; }
            Actors[i].Actor = monster;
            Actors[i].Origin = monster->K2_GetActorLocation();
            if (!first) first = &Actors[i].Origin;
            Report.Subjects[i].Spawned = true;
            if (auto* mesh = monster->Mesh) {
                // Dedicated servers may skip pose updates for unseen meshes; the probe needs them.
                mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
                const int count = (std::min)(mesh->GetNumBones(), QaProbePolicy::MaxBones + 1);
                for (int b = 1; b < count; ++b) Actors[i].Bones.push_back(mesh->GetBoneName(b));
            }
        }
        spawn.Seconds = Seconds(PhaseStart);
        if (spawn.Status == "running") spawn.Status = "ok";
        if (spawn.Status != "ok") { End(); return; }
        PhaseStart = GetTickCount64();
        Report.Begin("sample");
        NextSample = PhaseStart;
        SampleEnd = PhaseStart + static_cast<ULONGLONG>(Request.SampleSeconds) * 1000;
        Current = Phase::Sample;
        Write();
        return;
    }
    if (Current == Phase::Sample) {
        const auto now = GetTickCount64();
        if (now < NextSample) return;
        NextSample = now + 250;
        for (size_t i = 0; i < Actors.size(); ++i) Sample(Report.Subjects[i], Actors[i]);
        if (now < SampleEnd) { if ((now - PhaseStart) % 2000 < 250) Write(); return; }
        auto& stage = Report.Stages.back();
        stage.Seconds = Seconds(PhaseStart);
        stage.Status = "ok";
        for (size_t i = 0; i < Report.Subjects.size(); ++i) {
            const auto& subject = Report.Subjects[i];
            if (subject.Role != "creature") continue;
            const QaProbePolicy::MotionTrack* control = nullptr;
            for (const auto& other : Report.Subjects) if (other.Role == "control") control = &other.Track;
            const auto motion = QaProbePolicy::Judge(subject.Track, control);
            if (!Actors[i].Actor || Actors[i].Actor->IsActorBeingDestroyed()) Report.Fail(stage, "the creature was destroyed while sampling");
            else if (motion == QaProbePolicy::Motion::Frozen || motion == QaProbePolicy::Motion::NoSamples)
                Report.Fail(stage, std::string("creature motion: ") + QaProbePolicy::Name(motion));
            else if (motion == QaProbePolicy::Motion::Inconclusive) stage.Detail = "creature still; control did not move either (inconclusive)";
        }
        if (Report.Verdict == "running") Report.Verdict = "pass";
        Report.Begin("finish").Status = "ok";
        End();
    }
}
}
