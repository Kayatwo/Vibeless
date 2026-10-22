// QA engine probe (W2): on a dedicated arena server that `vibeless probe` starts for a qa-* profile,
// load every object of a creature package, spawn the creature (and an optional stock control), sample
// their poses, then stop the creature's AI and force every row of its action-set table (the row's
// montage, its notifies and spawned actors), damage every monster part and finally kill both
// subjects (the control shows whether a forced call works at all). reports/qa-probe-<id>.json is
// rewritten before and after every stage, so a crash or stall leaves the stage and asset in progress;
// the launcher adds exit code and crash evidence. Rules and report format: QaProbePolicy.h.
// AGPL-3.0-only.
#pragma once
#include "QaProbePolicy.h"
#include "QaProbeActionRoute.h"
#include "QaProbeActionTrace.h"
#include "FeatureSupport.h"
#include "HuntWorld.h"
#include "VibelessVersion.h"
#include "SDK/Engine_parameters.hpp"
#include "SDK/bp_gamemode_behemoth_spawner_component_parameters.hpp"
#include "SDK/bp_gamemode_behemoth_spawner_component_classes.hpp"
#include "SDK/NavigationSystem_classes.hpp"
#include "SDK/AIModule_classes.hpp"
#include "SDK/AIModule_parameters.hpp"
#include "SDK/monsterpart_bpc_classes.hpp"
#include "SDK/monsterpart_bpc_parameters.hpp"
#include "SDK/Struct_ActionSet_structs.hpp"

namespace QaProbe {
using namespace SDK;
enum class Phase { Start, World, Load, Spawn, Sample, Actions, Parts, Kill, Finish, Idle };
inline Phase Current = Phase::Start;
inline QaProbePolicy::Request Request;
inline QaProbePolicy::Report Report;
inline std::filesystem::path ReportPath;
inline ULONGLONG PhaseStart = 0, NextSample = 0, SampleEnd = 0;
struct Live { AArchonBehemoth* Actor = nullptr; FVector Origin{}; std::vector<FName> Bones; };
inline std::vector<Live> Actors;  // parallel to Report.Subjects
inline std::vector<std::pair<UObject*, int32>> Loaded;  // package objects that loaded, with their object index

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

// Stage "load": blocking loads on the game thread; the report names each object before it loads.
// `objects` receives what loaded (with its object index, to check it is still alive later).
inline void LoadStage(std::vector<std::pair<UObject*, int32>>* objects = nullptr) {
    PhaseStart = GetTickCount64();
    auto& stage = Report.Begin("load");
    for (const auto& asset : Request.Assets) {
        Report.CurrentAsset = asset;
        Write();
        const auto wide = Wide(asset);
        UObject* object = UKismetSystemLibrary::LoadAsset_Blocking(UKismetSystemLibrary::Conv_SoftObjPathToSoftObjRef(
            UKismetSystemLibrary::MakeSoftObjectPath(FString(wide.c_str()))));
        // Cooked Blueprint packages keep only their generated class: <package>.<name>_C.
        if (!object) if ((object = LoadClass(asset + "_C"))) ++Report.AssetsAsClass;
        if (object) { ++Report.AssetsLoaded; if (objects) objects->push_back({object, object->Index}); }
        else Report.FailedAssets.push_back(asset);
    }
    Report.CurrentAsset.clear();
    stage.Seconds = Seconds(PhaseStart);
    if (Report.FailedAssets.empty()) stage.Status = "ok";
    else Report.Fail(stage, std::to_string(Report.FailedAssets.size()) + " of " + std::to_string(Report.AssetsRequested) + " objects did not load");
}
// Stage "class": the creature (and control) classes load and derive from AArchonBehemoth. Adds one
// report subject per class; false when the probe cannot go on.
inline bool ClassStage(std::vector<std::pair<std::string, UClass*>>& types) {
    PhaseStart = GetTickCount64();
    auto& classes = Report.Begin("class");
    Write();
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
    return classes.Status == "ok";
}

// ---- Forced actions -------------------------------------------------------------------------
// One row of a package action-set table (row struct Struct_ActionSet): the attack class and its montage.
struct ActionRow { std::string Row; UClass* Action = nullptr; UAnimMontage* Montage = nullptr; float Rate = 1; size_t Subject = 0; };
inline std::vector<ActionRow> ActionRows;
inline size_t ActionIndex = 0;
inline ULONGLONG ActionStart = 0, ActionEnd = 0, NextAction = 0;
inline std::set<AActor*> ActionActors;  // owned or instigated by the creature before/while the action plays
inline int ActionNotifies = 0, ActionSpawned = 0;
inline QaProbeActionRoute::Live DynamicAction;

inline std::vector<ActionRow> ActionTable() {
    std::vector<ActionRow> rows;
    for (const auto& [object, index] : Loaded) {
        if (UObject::GObjects->GetByIndex(index) != object || !object->IsA(UDataTable::StaticClass())) continue;
        auto* table = static_cast<UDataTable*>(object);
        if (!table->RowStruct || table->RowStruct->GetName() != "Struct_ActionSet" || table->RowMap.NumAllocated() < 0 || table->RowMap.NumAllocated() > 1024) continue;
        for (int i = 0; i < table->RowMap.NumAllocated() && rows.size() < static_cast<size_t>(QaProbePolicy::MaxActions); ++i) {
            if (!table->RowMap.IsValidIndex(i)) continue;
            auto* row = reinterpret_cast<FStruct_ActionSet*>(table->RowMap[i].Value());
            if (!row) continue;
            const float rate = row->MontagePlayRate_8_A41689FE4E224D7BD13F658E91AA77C2;
            rows.push_back({table->RowMap[i].Key().ToString(), row->Class_38_A2E813EE429E126ADF2E999DA085A8F6,
                row->Montage_5_69C9740F43CE4938ABF4AEBB9EDD1279, rate > 0 && rate < 10 ? rate : 1.f});
        }
    }
    // A dynamic row also runs against the real stock-owned action instance. Do not copy the
    // candidate's action data into the control or create a fallback action/montage.
    const auto candidateRows = rows;
    for (const auto& row : candidateRows) if (!row.Montage)
        for (size_t i = 0; i < Actors.size(); ++i) if (Report.Subjects[i].Role == "control") {
            auto control = row; control.Subject = i; rows.push_back(control);
        }
    return rows;
}
// Actors the creature owns or instigated that are not yet in ActionActors; adds them.
inline int NewOwnedActors(UWorld* world, AArchonBehemoth* monster) {
    TArray<AActor*> actors;
    UGameplayStatics::GetAllActorsOfClass(world, AActor::StaticClass(), &actors);
    int added = 0;
    for (auto* actor : actors)
        if (actor && actor != monster && (actor->Owner == monster || actor->Instigator == monster) && ActionActors.insert(actor).second) ++added;
    return added;
}
struct LiveKill { size_t Subject = 0, Result = 0; };
inline std::vector<LiveKill> LiveKills;
// Server ProcessEvent, after the original: Blueprint notifies on the creature's mesh while an action
// plays (native notifies such as sounds do not pass through ProcessEvent), and functions named for
// death that run on each subject during the kill stage.
inline void ObserveServerEvent(UObject* object, UFunction* function, void* parameters) {
    QaProbeActionTrace::Observe(Report, object, function, true);
    if (!object || !function || Actors.empty()) return;
    if (Current == Phase::Kill) {
        for (const auto& live : LiveKills) {
            if (object != Actors[live.Subject].Actor) continue;
            auto& events = Report.Kills[live.Result].DeathEvents;
            const auto name = function->GetName();
            const bool death = name.find("Death") != std::string::npos || name.find("Died") != std::string::npos || name.find("Dead") != std::string::npos;
            if (death && events.size() < 8 && std::find(events.begin(), events.end(), name) == events.end()) events.push_back(name);
        }
        return;
    }
    if (Current != Phase::Actions || ActionEnd == 0 || !parameters || ActionIndex >= ActionRows.size()) return;
    auto* actor = Actors[ActionRows[ActionIndex].Subject].Actor;
    auto* mesh = actor ? actor->Mesh : nullptr;
    if (!mesh) return;
    const auto name = function->GetName();
    if (name == "Received_Notify" || name == "Received_NotifyBegin") { if (*static_cast<USkeletalMeshComponent**>(parameters) == mesh) ++ActionNotifies; }
    else if (name.starts_with("AnimNotify_") && object == mesh->GetAnimInstance()) ++ActionNotifies;
}
// Stops the creature's AI so that only the forced actions play. (AIModule's SDK functions are not
// compiled into the runtime; reflected calls go through ProcessEvent.)
inline void StopAi(AArchonBehemoth* monster) {
    auto* controller = monster ? monster->Controller : nullptr;
    if (!controller || !controller->IsA(AAIController::StaticClass())) return;
    auto* brain = static_cast<AAIController*>(controller)->BrainComponent;
    if (auto* stop = FeatureSupport::FindFunction(brain, "StopLogic")) {
        Params::BrainComponent_StopLogic parameters{};
        parameters.Reason = FString(L"qa-probe actions");
        brain->ProcessEvent(stop, &parameters);
    }
}
// One poll of the actions stage; true when it has finished.
inline bool TickActions(UWorld* world) {
    const auto now = GetTickCount64();
    const size_t subject = ActionIndex < ActionRows.size() ? ActionRows[ActionIndex].Subject : 0;
    auto* monster = Actors.empty() ? nullptr : Actors[subject].Actor;
    auto& stage = Report.Stages.back();
    if (!monster || monster->IsActorBeingDestroyed()) { Report.Fail(stage, "the creature was destroyed during the actions"); return true; }
    auto* anim = monster->Mesh ? monster->Mesh->GetAnimInstance() : nullptr;
    if (ActionEnd == 0) {
        if (now < NextAction) return false;
        if (ActionIndex >= ActionRows.size()) {
            stage.Seconds = Seconds(PhaseStart);
            const auto problem = QaProbePolicy::JudgeActions(Report.Actions);
            if (!problem.empty()) Report.Fail(stage, problem);
            else stage.Status = "ok";
            int notifies = 0, spawned = 0;
            for (const auto& a : Report.Actions) { notifies += a.Notifies; spawned += a.Spawned; }
            if (stage.Status == "ok") stage.Detail = std::to_string(Report.Actions.size()) + " actions, " + std::to_string(notifies) + " notifies, " + std::to_string(spawned) + " actors spawned";
            Report.CurrentAsset.clear();
            return true;
        }
        const auto& row = ActionRows[ActionIndex];
        QaProbePolicy::ActionResult result;
        result.Row = row.Row; result.Action = FeatureSupport::ObjectPath(row.Action); result.Montage = FeatureSupport::ObjectPath(row.Montage);
        result.Role = Report.Subjects[subject].Role;
        if (!row.Montage) result.Route = "action-class";
        Report.CurrentAsset = result.Montage.empty() ? result.Action : result.Montage;
        Report.Actions.push_back(result);
        Write();  // a crash while it plays leaves the montage in the report
        NewOwnedActors(world, monster);
        ActionNotifies = 0; ActionSpawned = 0;
        float length = 0;
        if (anim && row.Montage) length = anim->Montage_Play(row.Montage, row.Rate, EMontagePlayReturnType::MontageLength, 0.f, true);
        else if (!row.Montage) {
            QaProbeActionTrace::Snapshot(Report, monster, result.Role, "before-forced-action", row.Action, row.Row);
            Write();
            DynamicAction = QaProbeActionRoute::Begin(monster, row.Action); length = DynamicAction.Length;
        }
        auto& played = Report.Actions.back();
        played.Played = length > 0; played.Length = length;
        if (!row.Montage) {
            played.SelectedMontage = FeatureSupport::ObjectPath(DynamicAction.Montage);
            played.Error = DynamicAction.Error;
            played.Observed = anim && DynamicAction.Montage && anim->Montage_IsPlaying(DynamicAction.Montage);
            Write();
        }
        ActionStart = now;
        const double cap = (std::min)(QaProbePolicy::ActionCapSeconds, length / row.Rate + 1.0);
        ActionEnd = now + static_cast<ULONGLONG>((std::max)(0.25, cap) * 1000);
        return false;
    }
    ActionSpawned += NewOwnedActors(world, monster);
    const auto& row = ActionRows[ActionIndex];
    if (!row.Montage) {
        auto& result = Report.Actions.back();
        const bool playing = anim && DynamicAction.Montage && anim->Montage_IsPlaying(DynamicAction.Montage);
        result.Observed = result.Observed || playing;
        if (playing && now < ActionEnd) return false;
        result.Completed = result.Observed && !playing && now < ActionEnd;
        if (playing || (result.Observed && now >= ActionEnd)) result.Error = "action exceeded the bounded completion window";
        if (playing) anim->Montage_Stop(0.25f, DynamicAction.Montage);
        QaProbeActionRoute::Finish(DynamicAction);
        if (!DynamicAction.Error.empty()) result.Error = DynamicAction.Error;
    }
    if (now < ActionEnd && anim && row.Montage && anim->Montage_IsPlaying(row.Montage)) return false;
    auto& done = Report.Actions.back();
    done.Seconds = Seconds(ActionStart); done.Notifies = ActionNotifies; done.Spawned = ActionSpawned;
    ActionEnd = 0;
    ++ActionIndex;
    NextAction = now + 500;  // let the montage blend out before the next one
    Write();
    return false;
}

// ---- Part breaks and kill -------------------------------------------------------------------
// Both subjects get the same forced calls; the stock control shows whether the method works at all.
struct LivePart { Umonsterpart_bpc_C* Part = nullptr; int32 Index = -1; size_t Result = 0; bool SeveredBefore = false; };
inline std::vector<LivePart> LiveParts;
inline ULONGLONG StageEnd = 0;

// The subject's monster parts (the stock Blueprint part component).
inline std::vector<Umonsterpart_bpc_C*> PartsOf(AArchonBehemoth* monster) {
    std::vector<Umonsterpart_bpc_C*> parts;
    if (!monster || monster->IsActorBeingDestroyed()) return parts;
    auto components = monster->K2_GetComponentsByClass(Umonsterpart_bpc_C::StaticClass());
    if (components.Num() < 0 || components.Num() > QaProbePolicy::MaxParts) return parts;
    for (auto* component : components) if (component) parts.push_back(static_cast<Umonsterpart_bpc_C*>(component));
    return parts;
}
// GetMetadataTableName (behemoth interface): the key the actor's loot and metadata rows use (D33).
inline std::string MetadataKey(AArchonBehemoth* monster) {
    auto* get = FeatureSupport::FindFunction(monster, "GetMetadataTableName");
    if (!get) return {};
    alignas(16) unsigned char frame[512] = {};  // the out FName comes first; the rest is spare frame
    monster->ProcessEvent(get, frame);
    return reinterpret_cast<FName*>(frame)->ToString();
}
// Records every part with its break stages and loot names, then damages each breakable part by one
// and a half times its health through its own HandleDamage.
inline void BeginParts() {
    LiveParts.clear();
    for (size_t i = 0; i < Actors.size(); ++i) {
        for (auto* part : PartsOf(Actors[i].Actor)) {
            QaProbePolicy::PartResult result;
            result.Role = Report.Subjects[i].Role; result.Name = part->PartName.ToString();
            result.Stages = part->PartBreakData.Num(); result.Before = result.After = part->CurrentBreakStage;
            result.Health = part->UMonsterPartComponent::GetCurrentHealth(); result.MaxHealth = part->UMonsterPartComponent::GetMaxHealth();
            result.CanBeBroken = part->UMonsterPartComponent::CanBeBroken();
            result.Breakable = result.Before + 1 < result.Stages;
            if (auto* loot = FeatureSupport::FindFunction(part, "GetBreakLootName"))
                for (int stage = 0; stage < result.Stages && stage < 8; ++stage) {
                    Params::monsterpart_bpc_C_GetBreakLootName query{};
                    query.BreakIndex = stage;
                    part->ProcessEvent(loot, &query);
                    result.Loot.push_back(query.LootName.ToString());
                }
            Report.Parts.push_back(result);
            LiveParts.push_back({part, part->Index, Report.Parts.size() - 1, part->PartSevered});
        }
    }
    for (const auto& live : LiveParts) {
        const auto& result = Report.Parts[live.Result];
        auto* damage = result.Breakable ? FeatureSupport::FindFunction(live.Part, "HandleDamage") : nullptr;
        if (!damage) continue;
        Report.CurrentAsset = result.Role + " part " + result.Name;
        Write();  // a crash while it breaks leaves the part in the report
        Params::monsterpart_bpc_C_HandleDamage hit{};
        hit.DamageAmount = static_cast<float>((std::max)({result.MaxHealth, result.Health, 1.0}) * 1.5 + 1);
        hit.ServerHitLoc = live.Part->K2_GetComponentLocation();
        live.Part->ProcessEvent(damage, &hit);
    }
    Report.CurrentAsset.clear();
}
inline void EndParts() {
    for (const auto& live : LiveParts) {
        if (UObject::GObjects->GetByIndex(live.Index) != live.Part) continue;
        auto& result = Report.Parts[live.Result];
        result.After = live.Part->CurrentBreakStage;
        result.Severed = !live.SeveredBefore && live.Part->PartSevered;
    }
}
// Lethal core damage, ten times the maximum health, through a part's ApplyDamageToCore.
inline void BeginKill() {
    LiveKills.clear();
    for (size_t i = 0; i < Actors.size(); ++i) {
        auto* monster = Actors[i].Actor;
        if (!monster || monster->IsActorBeingDestroyed()) continue;
        QaProbePolicy::KillResult result;
        result.Role = Report.Subjects[i].Role;
        result.MetadataKey = MetadataKey(monster);
        result.HealthBefore = monster->GetCurrentHealth();
        Report.Kills.push_back(result);
        LiveKills.push_back({i, Report.Kills.size() - 1});
        const auto parts = PartsOf(monster);
        auto* core = parts.empty() ? nullptr : FeatureSupport::FindFunction(parts[0], "ApplyDamageToCore");
        if (!core) continue;
        Report.CurrentAsset = result.Role + " kill";
        Write();
        Params::monsterpart_bpc_C_ApplyDamageToCore hit{};
        hit.BaseDamage = (std::max)(monster->GetMaxHealth(), 1.0f) * 10;
        parts[0]->ProcessEvent(core, &hit);
    }
    Report.CurrentAsset.clear();
}
// True once every subject died or the wait ended.
inline bool PollKill(bool last) {
    bool all = true;
    for (const auto& live : LiveKills) {
        auto& result = Report.Kills[live.Result];
        auto* monster = Actors[live.Subject].Actor;
        const bool gone = !monster || UObject::GObjects->GetByIndex(monster->Index) != monster || monster->IsActorBeingDestroyed();
        if (!result.Died) {
            result.HealthAfter = gone ? 0 : monster->GetCurrentHealth();
            result.Died = gone || result.HealthAfter <= 0;
            result.Seconds = Seconds(PhaseStart);
        }
        all = all && result.Died;
    }
    return all || last;
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
        LoadStage(&Loaded);
        std::vector<std::pair<std::string, UClass*>> types;
        if (!ClassStage(types)) { End(); return; }

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
            QaProbeActionTrace::SpawningClass = types[i].second;
            QaProbeActionTrace::SpawningRole = Report.Subjects[i].Role;
            auto* monster = Spawn(world, types[i].second, location, error);
            QaProbeActionTrace::SpawningClass = nullptr;
            if (!monster) { Report.Fail(spawn, types[i].first + ": " + error); break; }
            QaProbeActionTrace::Register(monster, Report.Subjects[i].Role);
            QaProbeActionTrace::Snapshot(Report, monster, Report.Subjects[i].Role, "spawn-return");
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
        // Forced actions: only for a creature that animates (a frozen one has already failed).
        ActionRows = Report.Verdict == "running" ? ActionTable() : std::vector<ActionRow>{};
        if (!ActionRows.empty()) {
            for (const auto& row : ActionRows) if (!row.Montage)
                QaProbeActionTrace::Snapshot(Report, Actors[row.Subject].Actor, Report.Subjects[row.Subject].Role,
                    "before-stop-ai", row.Action, row.Row);
            PhaseStart = GetTickCount64();
            Report.Begin("actions");
            for (const auto& actor : Actors) StopAi(actor.Actor);
            NextAction = PhaseStart + 1000;  // the stopped behaviour's montage blends out
            Current = Phase::Actions;
            Write();
            return;
        }
        if (Report.Verdict == "running") Report.Verdict = "pass";
        Report.Begin("finish").Status = "ok";
        End();
        return;
    }
    if (Current == Phase::Actions) {
        if (!TickActions(world)) return;
        PhaseStart = GetTickCount64();
        Report.Begin("parts");
        Write();
        BeginParts();
        StageEnd = GetTickCount64() + 1500;  // breaks replicate and swap materials over a few frames
        Current = Phase::Parts;
        Write();
        return;
    }
    if (Current == Phase::Parts) {
        if (GetTickCount64() < StageEnd) return;
        EndParts();
        auto& stage = Report.Stages.back();
        stage.Seconds = Seconds(PhaseStart);
        if (const auto problem = QaProbePolicy::JudgeParts(Report.Parts); !problem.empty()) Report.Fail(stage, problem);
        else {
            stage.Status = "ok";
            int creature = 0, broke = 0, control = 0, controlBroke = 0;
            for (const auto& p : Report.Parts) {
                if (p.Role == "creature") { ++creature; broke += p.Broke(); }
                else { ++control; controlBroke += p.Broke(); }
            }
            stage.Detail = "creature " + std::to_string(broke) + " of " + std::to_string(creature) + " parts broke; control "
                + std::to_string(controlBroke) + " of " + std::to_string(control);
        }
        PhaseStart = GetTickCount64();
        Report.Begin("kill");
        Write();
        BeginKill();
        StageEnd = GetTickCount64() + 8000;
        Current = Phase::Kill;
        Write();
        return;
    }
    if (Current == Phase::Kill) {
        if (!PollKill(GetTickCount64() >= StageEnd)) return;
        auto& stage = Report.Stages.back();
        stage.Seconds = Seconds(PhaseStart);
        if (const auto problem = QaProbePolicy::JudgeKill(Report.Kills); !problem.empty()) Report.Fail(stage, problem);
        else {
            stage.Status = "ok";
            for (const auto& k : Report.Kills) stage.Detail += (stage.Detail.empty() ? "" : "; ") + k.Role + (k.Died ? " died" : " survived") + " (" + k.MetadataKey + ")";
        }
        if (Report.Verdict == "running") Report.Verdict = "pass";
        Report.Begin("finish").Status = "ok";
        End();
    }
}
}
