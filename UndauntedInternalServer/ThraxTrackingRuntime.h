// Private QA-gated tracking adapter. AGPL-3.0-only.
// Offsets are measured against the pinned executable, not inferred from UE headers.
#pragma once
#include "ThraxTrackingRuntimePolicy.h"
#include "ThraxTrackingKernel.h"
#include "ThraxTrackingProbe.h"
#include <atomic>

namespace ThraxTrackingRuntime {
using namespace SDK;
namespace ReadOnly = ThraxTrackingProbe;
namespace Math = feareater_tracking;
using NodeCall = void (*)(void*, void*);
using EvaluationCall = void (*)(void*, void*, void*);
using DestroyCall = void* (*)(void*, unsigned int);
inline NodeCall OriginalInitialize = nullptr, OriginalUpdate = nullptr;
inline EvaluationCall OriginalEvaluate = nullptr;
inline DestroyCall OriginalDestroy = nullptr;
inline void (*AssignWeak)(WeakHandle*, UObject*) = nullptr;
inline UObject* (*ResolveWeak)(const WeakHandle*) = nullptr;
inline std::uintptr_t Base = 0;
inline std::atomic<bool> Enabled = false;
inline State Instances;
inline std::atomic<std::uint64_t> Initialized = 0, Updated = 0, ActiveUpdates = 0, Applied = 0, Rejected = 0, Ended = 0;

struct Scope { Abi::Node Node; void* Proxy = nullptr; UObject* Instance = nullptr; };
inline bool ScopeOf(void* node, void* context, Scope& scope) {
    std::uintptr_t vtable = 0;
    if (!ReadOnly::Read(node, vtable) || vtable != Base + NodeVtableRva
        || !ReadOnly::Read(node, scope.Node) || ReadOnly::Name(scope.Node.PoseName) != Marker
        || !ReadOnly::Read(context, scope.Proxy) || !scope.Proxy
        || !ReadOnly::Read(ReadOnly::At(scope.Proxy, 0xa0), scope.Instance)
        || !ReadOnly::Readable(scope.Instance, sizeof(UObject))
        || !ReadOnly::Readable(scope.Instance->Class, sizeof(UStruct))) return false;
    std::string path;
    if (!ReadOnly::ObjectPath(scope.Instance->Class, path) || path != AnimationClass) return false;
    auto* property = ReadOnly::Property(scope.Instance->Class, NodeName, "StructProperty");
    return ReadOnly::Struct(property, "AnimNode_ArchonSaveComponentBoneTransform")
        && property->ElementSize == static_cast<int>(sizeof(Abi::Node))
        && ReadOnly::At(scope.Instance, static_cast<size_t>(property->Offset)) == node;
}
inline std::optional<Token> Binding(void* node, const Scope& scope) {
    const auto token = Instances.BindingAt(reinterpret_cast<std::uintptr_t>(node));
    if (!token) return {};
    const auto& owner = token->Identity;
    WeakHandle weak{owner.Index, owner.Serial};
    if (owner.Instance != reinterpret_cast<std::uintptr_t>(scope.Instance)
        || owner.Proxy != reinterpret_cast<std::uintptr_t>(scope.Proxy)
        || owner.Index != scope.Instance->Index || ResolveWeak(&weak) != scope.Instance) return {};
    return token;
}
inline void InitializeHook(void* node, void* context) {
    OriginalInitialize(node, context);
    if (!Enabled.load(std::memory_order_acquire)) return;
    try {
        Scope scope;
        if (!ScopeOf(node, context, scope)) return;
        Instances.End(reinterpret_cast<std::uintptr_t>(node));
        WeakHandle weak; AssignWeak(&weak, scope.Instance);
        if (ResolveWeak(&weak) != scope.Instance) { ++Rejected; return; }
        Owner owner{reinterpret_cast<std::uintptr_t>(scope.Instance), reinterpret_cast<std::uintptr_t>(node),
            reinterpret_cast<std::uintptr_t>(scope.Proxy), weak.Index, weak.Serial};
        if (Instances.Initialize(owner)) ++Initialized; else ++Rejected;
    } catch (...) { ++Rejected; }
}
inline void* DestroyHook(void* node, unsigned int flags) {
    // CppStructOps::Destruct dispatches this same virtual slot with flags=0.
    // End precedes the native array frees, including embedded-node teardown.
    if (Enabled.load(std::memory_order_acquire)) {
        try {
            if (Instances.BindingAt(reinterpret_cast<std::uintptr_t>(node))) {
                Instances.End(reinterpret_cast<std::uintptr_t>(node)); ++Ended;
            }
        } catch (...) { ++Rejected; }
    }
    return OriginalDestroy(node, flags);
}
inline void UpdateHook(void* node, void* context) {
    OriginalUpdate(node, context);
    if (!Enabled.load(std::memory_order_acquire)) return;
    try {
        Scope scope;
        if (!ScopeOf(node, context, scope)) return;
        auto* actualAlpha = reinterpret_cast<float*>(static_cast<std::byte*>(node) + 0x24);
        const auto binding = Binding(node, scope);
        if (!binding) { *actualAlpha = 0; ++Rejected; return; }
        Abi::Sample raw;
        UpdateContext update;
        Traversal stamp;
        if (!ReadOnly::Read(context, update) || update.Proxy != scope.Proxy
            || !ReadOnly::Read(ReadOnly::At(scope.Proxy, 0x10), raw.Component)
            || !ReadOnly::Read(ReadOnly::At(scope.Proxy, UpdateTraversalOffset), stamp) || !stamp.Valid()
            || !ReadOnly::Inputs(scope.Instance, scope.Instance->Class, raw)
            || !Abi::ValidArray(scope.Node.Bones, 7) || scope.Node.Bones.Count != 7) {
            Instances.Reset(*binding); *actualAlpha = 0; ++Rejected; return;
        }
        const auto alpha = EffectiveAlpha(scope.Node.ActualAlpha, raw.LookAtAlpha, raw.CurveAlpha);
        Snapshot snapshot{raw.Target, {raw.Component.Rotation, raw.Component.Translation, raw.Component.Scale}, alpha.value_or(0), update.Delta, stamp};
        Traversal afterInputs;
        if (!alpha || !ReadOnly::Read(ReadOnly::At(scope.Proxy, UpdateTraversalOffset), afterInputs) || !(afterInputs == stamp)
            || !Instances.Update(*binding, snapshot)) {
            Instances.Reset(*binding); *actualAlpha = 0; ++Rejected; return;
        }
        // Float Alpha=1 is authored on the carrier. Preserve the native LOD gate,
        // then replace only its already-clamped actual alpha with this update's
        // copied product. The outer engine evaluator applies it exactly once.
        *actualAlpha = *alpha; ++Updated; if (*alpha > RelevanceThreshold) ++ActiveUpdates;
    } catch (...) {
        // A recognized binding can never retain time/history after failed input.
        const auto binding = Instances.BindingAt(reinterpret_cast<std::uintptr_t>(node));
        if (binding) { Instances.Reset(*binding); *reinterpret_cast<float*>(static_cast<std::byte*>(node) + 0x24) = 0; }
        ++Rejected;
    }
}
inline Math::Transform ToMath(const Abi::Transform& value) { return {value.Rotation, value.Translation, value.Scale}; }
inline Abi::Transform FromMath(const Math::Transform& value) { return {value.q, value.t, 0, value.s, 0}; }

struct AppliedSample {
    std::uint64_t Epoch = 0, Generation = 0;
    std::int32_t InstanceIndex = -1;
    Snapshot Input;
    float Delta = 0, Pitch = 0, Yaw = 0;
    Quaternion Previous = Identity, Next = Identity;
    std::array<BoneTransform, 7> Before{}, After{};
};
inline std::mutex AuditMutex;
inline std::array<AppliedSample, 32> AuditSamples{};
inline std::array<std::tuple<std::uint64_t, std::uint64_t, std::int16_t>, 32> AuditKeys{};
inline size_t AuditReserved = 0, AuditPending = 0;
inline std::wstring AuditFile;
inline unsigned AuditReports = 0;
inline ULONGLONG LastAuditTime = 0;
inline void Record(AppliedSample sample) {
    std::lock_guard lock(AuditMutex);
    if (AuditReserved == AuditSamples.size()) return;
    const auto key = std::tuple{sample.Generation, sample.Input.Stamp.Frame, sample.Input.Stamp.Counter};
    for (size_t i = 0; i < AuditReserved; ++i) if (AuditKeys[i] == key) return;
    AuditKeys[AuditReserved++] = key; AuditSamples[AuditPending++] = sample;
}
inline void EvaluateHook(void* node, void* contextPointer, void* outputPointer) {
    Scope scope;
    std::optional<Token> binding;
    std::optional<Ticket> ticket;
    Traversal stamp;
    if (Enabled.load(std::memory_order_acquire)) {
        try {
            if (ScopeOf(node, contextPointer, scope)) {
                binding = Binding(node, scope);
                if (binding && ReadOnly::Read(ReadOnly::At(scope.Proxy, UpdateTraversalOffset), stamp))
                    ticket = Instances.Prepare(*binding, stamp);
            }
        } catch (...) { ++Rejected; }
    }
    // The stock save callback materializes current component-space poses and
    // preserves its own private saved-pose behavior. Call it once on every path.
    OriginalEvaluate(node, contextPointer, outputPointer);
    if (!Enabled.load(std::memory_order_acquire)) return;
    if (!ticket) { if (binding) ++Rejected; return; }
    try {
        auto reject = [&] { Instances.Reset(*ticket); ++Rejected; };
        Abi::Context context;
        Traversal currentStamp;
        Abi::Array<std::int32_t> parents;
        if (!ReadOnly::Read(contextPointer, context) || context.Proxy != scope.Proxy || !context.BoneContainer
            || !ReadOnly::Read(ReadOnly::At(scope.Proxy, UpdateTraversalOffset), currentStamp) || !(currentStamp == stamp)
            || !ReadOnly::Read(ReadOnly::At(context.BoneContainer, 0x90), parents)
            || !Abi::ValidArray(parents) || !Abi::ValidArray(context.Pose) || !Abi::ValidArray(context.Flags)
            || parents.Count != context.Pose.Count || parents.Count != context.Flags.Count
            || !Abi::ValidArray(scope.Node.Bones, 7) || scope.Node.Bones.Count != 7
            || outputPointer != ReadOnly::At(node, 0xb8)) { reject(); return; }
        std::array<std::int32_t, 7> indices{}, parentIndices{};
        Math::Input input;
        AppliedSample sample;
        for (size_t i = 0; i < 7; ++i) {
            Abi::BoneReference bone;
            std::uint8_t flag = 0;
            Abi::Transform transform;
            if (!ReadOnly::Read(scope.Node.Bones.Data + i, bone) || ReadOnly::Name(bone.Name) != Abi::BoneNames[i]
                || !Abi::ValidIndex(bone, context) || !ReadOnly::Read(parents.Data + bone.CompactIndex, parentIndices[i])
                || !ReadOnly::Read(context.Flags.Data + bone.CompactIndex, flag) || flag != 1
                || !ReadOnly::Read(context.Pose.Data + bone.CompactIndex, transform) || !Abi::Finite(transform)) { reject(); return; }
            indices[i] = bone.CompactIndex; input.components[i] = ToMath(transform);
            sample.Before[i].Index = bone.CompactIndex; sample.Before[i].Value = transform;
        }
        if (!OrderedChain(indices, parentIndices)) { reject(); return; }
        for (size_t i = 1; i < 7; ++i) input.locals[i] = ThraxTrackingKernel::Relative(input.components[i], input.components[i - 1]);
        auto* output = static_cast<Abi::Array<BoneTransform>*>(outputPointer);
        if (!ReadOnly::Readable(output, sizeof(*output)) || output->Count != 0 || output->Capacity < 0 || output->Capacity > 65536
            || (output->Capacity && !ReadOnly::Readable(output->Data, sizeof(BoneTransform) * static_cast<size_t>(output->Capacity)))) { reject(); return; }
        if (!ReadOnly::Read(ReadOnly::At(scope.Proxy, UpdateTraversalOffset), currentStamp) || !(currentStamp == stamp)) { reject(); return; }
        const bool applied = Instances.Evaluate(*ticket, [&](const Snapshot& snapshot, float delta, Quaternion previous, Quaternion& next, std::uint64_t epoch) {
            input.frame = {snapshot.Component.Rotation, snapshot.Component.Translation, snapshot.Component.Scale};
            input.target_world = snapshot.Target; input.previous = previous; input.delta = delta;
            const auto result = ThraxTrackingKernel::Solve(input);
            if (result.status != Math::Status::ok || !ThraxTracking::Finite(result.offset)) return false;
            for (size_t i = 0; i < 7; ++i) { sample.After[i].Index = indices[i]; sample.After[i].Value = FromMath(result.poses[i]); }
            if (!PublishOutput(*output, sample.After, [](void* old, size_t bytes, size_t alignment) {
                return UC::FMemory::Realloc(old, bytes, static_cast<UC::uint32>(alignment));
            })) return false;
            next = result.offset;
            sample.Epoch = epoch; sample.Generation = binding->Generation; sample.InstanceIndex = binding->Identity.Index;
            sample.Input = snapshot; sample.Delta = delta; sample.Pitch = result.pitch; sample.Yaw = result.yaw;
            sample.Previous = previous; sample.Next = next; return true;
        });
        if (applied) { ++Applied; Record(sample); } else ++Rejected;
    } catch (...) {
        if (ticket) Instances.Reset(*ticket);
        ++Rejected;
    }
}
inline bool Install(std::uintptr_t base, FeatureHost::Side side) {
    auto profile = std::filesystem::path(ProfileRoot::Root()).lexically_normal();
    if (profile.filename().empty()) profile = profile.parent_path();
    wchar_t optIn[8]{};
    if (GetEnvironmentVariableW(L"DR_THRAX_TRACKING", optIn, 8) != 1 || !Abi::QaProfile(profile.filename().wstring(), optIn)
        || Vibeless::Features().Enabled("thrax-tracking-probe")) return false;
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768 || ReadOnly::HashFile(executable) != Abi::ExecutableSha256 || !UC::FMemory::EngineRealloc) return false;
    struct Hook { std::uintptr_t Rva; void* Detour; void** Original; const char* Name; std::array<unsigned char, 16> Signature; };
    const std::array<Hook, 4> hooks{{
        {InitializeRva, reinterpret_cast<void*>(&InitializeHook), reinterpret_cast<void**>(&OriginalInitialize), "Thrax.Initialize", {0xe9,0xcb,0x24,0x2d,0x02,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}},
        {UpdateRva, reinterpret_cast<void*>(&UpdateHook), reinterpret_cast<void**>(&OriginalUpdate), "Thrax.Update", {0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0x01,0x48,0x8b,0xfa}},
        {DestroyRva, reinterpret_cast<void*>(&DestroyHook), reinterpret_cast<void**>(&OriginalDestroy), "Thrax.Destroy", {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x8b,0xfa,0x48}},
        {Abi::CallbackRva, reinterpret_cast<void*>(&EvaluateHook), reinterpret_cast<void**>(&OriginalEvaluate), "Thrax.Evaluate", {0x40,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x48,0x83,0xec,0x30,0x48,0x8b,0x3a,0x4c}}
    }};
    auto& host = Vibeless::Features();
    for (const auto& hook : hooks) {
        std::array<unsigned char, 16> actual{};
        if (!ReadOnly::Read(reinterpret_cast<void*>(base + hook.Rva), actual) || actual != hook.Signature) return false;
        for (const auto& claim : host.Hooks()) if (claim.Target == base + hook.Rva) return false;
    }
    Base = base;
    AssignWeak = reinterpret_cast<decltype(AssignWeak)>(base + WeakAssignRva);
    ResolveWeak = reinterpret_cast<decltype(ResolveWeak)>(base + WeakGetRva);
    if (!FName::AppendString) FName::InitInternal();
    size_t created = 0;
    bool success = true;
    for (const auto& hook : hooks) {
        const auto error = host.Claim(base + hook.Rva, hook.Name, "thrax-tracking");
        if (!error.empty()) { host.AddError(error); success = false; break; }
        if (MH_CreateHook(reinterpret_cast<void*>(base + hook.Rva), hook.Detour, hook.Original) != MH_OK) { success = false; break; }
        ++created;
        if (MH_QueueEnableHook(reinterpret_cast<void*>(base + hook.Rva)) != MH_OK) { success = false; break; }
    }
    if (success) success = MH_ApplyQueued() == MH_OK;
    if (!success) {
        for (size_t i = 0; i < created; ++i) {
            MH_DisableHook(reinterpret_cast<void*>(base + hooks[i].Rva)); MH_RemoveHook(reinterpret_cast<void*>(base + hooks[i].Rva));
        }
    }
    for (const auto& hook : hooks) host.MarkHook(base + hook.Rva, success, success ? "" : "tracking hook transaction failed; adapter disabled");
    if (!success) return false;
    AuditFile = L"thrax-tracking-" + std::wstring(side == FeatureHost::Side::Client ? L"client-" : L"server-")
        + std::to_wstring(GetCurrentProcessId()) + L".jsonl";
    Enabled.store(true, std::memory_order_release); return true;
}
inline void Tick() {
    if (AuditFile.empty()) return;
    std::error_code error;
    const auto directory = std::filesystem::path(ProfileRoot::Root()) / L"reports";
    std::filesystem::create_directories(directory, error); if (error) return;
    std::ofstream out(directory / AuditFile, std::ios::app); if (!out) return;
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    const auto now = GetTickCount64();
    if (AuditReports < 32 && (!LastAuditTime || now - LastAuditTime >= 5000)) {
        ++AuditReports; LastAuditTime = now;
        out << "{\"event\":\"tracking_counts\",\"initialized\":" << Initialized.load() << ",\"updated\":" << Updated.load()
            << ",\"activeUpdates\":" << ActiveUpdates.load() << ",\"applied\":" << Applied.load()
            << ",\"rejected\":" << Rejected.load() << ",\"ended\":" << Ended.load() << "}\n";
    }
    std::array<AppliedSample, 32> samples;
    size_t count = 0;
    { std::lock_guard lock(AuditMutex); count = AuditPending; std::copy_n(AuditSamples.begin(), count, samples.begin()); AuditPending = 0; }
    for (size_t i = 0; i < count; ++i) {
        const auto& sample = samples[i];
        out << "{\"event\":\"tracking_applied\",\"epoch\":" << sample.Epoch << ",\"generation\":" << sample.Generation
            << ",\"updateFrame\":" << sample.Input.Stamp.Frame << ",\"updateCounter\":" << sample.Input.Stamp.Counter
            << ",\"instanceIndex\":" << sample.InstanceIndex << ",\"alpha\":" << sample.Input.Alpha << ",\"delta\":" << sample.Delta
            << ",\"pitch\":" << sample.Pitch << ",\"yaw\":" << sample.Yaw << ",\"target\":";
        ReadOnly::ArrayJson(out, sample.Input.Target); out << ",\"previous\":"; ReadOnly::ArrayJson(out, sample.Previous);
        out << ",\"next\":"; ReadOnly::ArrayJson(out, sample.Next); out << ",\"component\":";
        ReadOnly::TransformJson(out, {sample.Input.Component.Rotation, sample.Input.Component.Translation, 0, sample.Input.Component.Scale, 0});
        out << ",\"bones\":[";
        for (size_t bone = 0; bone < 7; ++bone) {
            if (bone) out << ',';
            out << "{\"index\":" << sample.Before[bone].Index << ",\"before\":"; ReadOnly::TransformJson(out, sample.Before[bone].Value);
            out << ",\"output\":"; ReadOnly::TransformJson(out, sample.After[bone].Value); out << '}';
        }
        out << "]}\n";
    }
}
}
