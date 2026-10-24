// Observation only: no bone, node, graph, actor, or saved-pose writes. AGPL-3.0-only.
// Loaded after Vibeless::Hook is declared. No UObject pointer survives a callback.
#pragma once
#include "ThraxTrackingProbePolicy.h"
#include "FeatureSupport.h"
#include <bcrypt.h>
#include <iomanip>
#pragma comment(lib, "bcrypt.lib")

namespace ThraxTrackingProbe {
using namespace SDK;
namespace Policy = ThraxTrackingProbePolicy;
using Callback = void (*)(void*, void*, void*);
inline Callback Original = nullptr;
inline Policy::Queue Samples;
inline std::wstring ReportFile;
inline bool ReportStarted = false;

inline bool Readable(const void* pointer, size_t bytes) {
    auto address = reinterpret_cast<std::uintptr_t>(pointer);
    if (!address || !bytes || bytes > 1024 * 1024 || address > (std::numeric_limits<std::uintptr_t>::max)() - bytes) return false;
    const auto end = address + bytes;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof region) || region.State != MEM_COMMIT
            || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        const DWORD access = region.Protect & 0xff;
        if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY
            && access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
        const auto next = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (next <= address) return false;
        address = next;
    }
    return true;
}
template<typename T> inline bool Read(const void* pointer, T& result) {
    if (!Readable(pointer, sizeof(T))) return false;
    std::memcpy(&result, pointer, sizeof(T)); return true;
}
inline const std::byte* At(const void* base, size_t offset) { return static_cast<const std::byte*>(base) + offset; }
inline std::string Name(std::uint64_t bits) {
    static_assert(sizeof(FName) == sizeof(bits));
    FName value; std::memcpy(&value, &bits, sizeof(value)); return value.ToString();
}
// Reflection belongs to this currently evaluating instance's live class. The
// traversal neither invokes ProcessEvent nor touches actors or cached bindings.
inline FProperty* Property(UStruct* type, std::string_view name, std::string_view fieldType) {
    for (size_t depth = 0; type && depth < 32; ++depth, type = type->SuperStruct) {
        if (!Readable(type, sizeof(UStruct))) return nullptr;
        size_t count = 0;
        for (auto* field = type->ChildProperties; field && count++ < 4096; field = field->Next) {
            if (!Readable(field, sizeof(FProperty)) || !Readable(field->ClassPrivate, sizeof(FFieldClass))) return nullptr;
            if (field->Name.ToString() == name && field->ClassPrivate->Name.ToString() == fieldType) {
                auto* property = static_cast<FProperty*>(field);
                if (property->ArrayDim != 1 || property->Offset < 0 || property->ElementSize <= 0
                    || type->Size <= 0 || property->Offset > type->Size - property->ElementSize) return nullptr;
                return property;
            }
        }
    }
    return nullptr;
}
inline UStruct* Struct(FProperty* property, std::string_view name = {}) {
    if (!property || !Readable(property, sizeof(FStructProperty))) return nullptr;
    auto* type = static_cast<FStructProperty*>(property)->Struct;
    if (!Readable(type, sizeof(UStruct)) || type->Size != property->ElementSize) return nullptr;
    return name.empty() || type->Name.ToString() == name ? type : nullptr;
}
inline bool ObjectPath(UObject* object, std::string& result) {
    if (!Readable(object, sizeof(UObject))) return false;
    auto* package = object;
    for (size_t depth = 0; depth < 32; ++depth) {
        if (!Readable(package, sizeof(UObject))) return false;
        if (!package->Outer) { result = package->Name.GetRawString() + "." + object->Name.ToString(); return true; }
        package = package->Outer;
    }
    return false;
}
inline bool Inputs(UObject* instance, UStruct* type, Policy::Sample& sample) {
    auto* variables = Property(type, "Behemoth Variables", "StructProperty");
    auto* variablesType = Struct(variables);
    if (!variablesType) return false;
    auto* target = Property(variablesType, "SmoothedLookAtTarget_21_F23C96814CF5E73C2CAC27B247A28BA3", "StructProperty");
    auto* alpha = Property(variablesType, "LookAtAlpha_9_8DB1EFC143E1728209E6A5A4A0BF12B9", "FloatProperty");
    auto* curve = Property(variablesType, "CurveAlpha_18_DF7504AB4C889685387854A1A524D01F", "FloatProperty");
    if (!Struct(target, "Vector") || target->ElementSize != 12 || !alpha || alpha->ElementSize != 4 || !curve || curve->ElementSize != 4) return false;
    const auto* data = At(instance, static_cast<size_t>(variables->Offset));
    return Read(At(data, static_cast<size_t>(target->Offset)), sample.Target)
        && Read(At(data, static_cast<size_t>(alpha->Offset)), sample.LookAtAlpha)
        && Read(At(data, static_cast<size_t>(curve->Offset)), sample.CurveAlpha);
}
inline void Capture(void* nodePointer, void* contextPointer) {
    if (Samples.Full()) return;
    Policy::Node node;
    if (!Read(nodePointer, node) || Name(node.PoseName) != ThraxTracking::Marker) return;
    Policy::Context context;
    UObject* instance = nullptr;
    if (!Read(contextPointer, context) || !context.Proxy || !Read(At(context.Proxy, 0xa0), instance)
        || !Readable(instance, sizeof(UObject)) || !Readable(instance->Class, sizeof(UStruct))) return;
    std::string classPath;
    if (!ObjectPath(instance->Class, classPath) || classPath != ThraxTracking::AnimationClass) return;
    const auto reserved = Samples.Reserve();
    if (!reserved) return;
    Policy::Sample sample;
    sample.Ordinal = *reserved; sample.ThreadId = GetCurrentThreadId();
    sample.InstanceIndex = instance->Index; sample.ClassSize = instance->Class->Size;
    auto reject = [&](Policy::Rejection reason) { sample.Rejected = reason; Samples.Publish(sample); };
    try {
        auto* carrier = Property(instance->Class, ThraxTracking::NodeName, "StructProperty");
        if (carrier) {
            sample.NodeOffset = carrier->Offset;
            sample.PropertyElementSize = carrier->ElementSize;
            if (Readable(carrier, sizeof(FStructProperty))) {
                auto* nativeType = static_cast<FStructProperty*>(carrier)->Struct;
                if (Readable(nativeType, sizeof(UStruct))) sample.NativeStructSize = nativeType->Size;
            }
        }
        // Cooked declarations record 0x140; the executable and SDK measure 0xe0.
        // Observe runtime linking without guessing: record a bounded rejection if
        // the live property/native sizes or exact reflected node identity differ.
        if (!Struct(carrier, "AnimNode_ArchonSaveComponentBoneTransform") || carrier->ElementSize != static_cast<int>(sizeof(Policy::Node))
            || At(instance, static_cast<size_t>(carrier->Offset)) != nodePointer) { reject(Policy::Rejection::Reflection); return; }
        sample.ActualAlpha = node.ActualAlpha;
        if (!Policy::ValidArray(node.Bones, 7) || node.Bones.Count != 7 || !context.BoneContainer
            || !Policy::ValidArray(context.Pose) || !Policy::ValidArray(context.Flags)
            || !Read(At(context.Proxy, 0x10), sample.Component)) { reject(Policy::Rejection::Layout); return; }
        if (!Inputs(instance, instance->Class, sample)) { reject(Policy::Rejection::Reflection); return; }
        for (size_t index = 0; index < Policy::BoneCount; ++index) {
            Policy::BoneReference bone;
            if (!Read(node.Bones.Data + index, bone) || Name(bone.Name) != Policy::BoneNames[index]) { reject(Policy::Rejection::Reflection); return; }
            if (bone.IndexMode < 0) { reject(Policy::Rejection::UnsupportedIndexMode); return; }
            if (!Policy::ValidIndex(bone, context)) { reject(Policy::Rejection::BoneIndex); return; }
            for (size_t previous = 0; previous < index; ++previous)
                if (sample.CompactIndices[previous] == bone.CompactIndex) { reject(Policy::Rejection::BoneIndex); return; }
            std::uint8_t flag = 0;
            if (!Read(context.Flags.Data + bone.CompactIndex, flag) || flag != 1) { reject(Policy::Rejection::ComponentSpace); return; }
            sample.CompactIndices[index] = bone.CompactIndex;
            if (!Read(context.Pose.Data + bone.CompactIndex, sample.Bones[index])) { reject(Policy::Rejection::Layout); return; }
            if (!Policy::Finite(sample.Bones[index])) { reject(Policy::Rejection::Nonfinite); return; }
        }
        if (!std::isfinite(sample.ActualAlpha) || !std::isfinite(sample.LookAtAlpha) || !std::isfinite(sample.CurveAlpha)
            || !ThraxTracking::Finite(sample.Target) || !Policy::Finite(sample.Component)) { reject(Policy::Rejection::Nonfinite); return; }
        Samples.Publish(sample);
    } catch (...) { reject(Policy::Rejection::Exception); }
}
inline void CallbackHook(void* node, void* context, void* output) {
    Policy::AfterOriginal([&] { Original(node, context, output); }, [&] { Capture(node, context); });
}
inline std::string HashFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0
        && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::array<char, 65536> buffer{};
        bool failed = false;
        while (input) {
            input.read(buffer.data(), buffer.size());
            const auto count = static_cast<ULONG>(input.gcount());
            if (count && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), count, 0) != 0) { failed = true; break; }
        }
        std::array<unsigned char, 32> digest{};
        if (!failed && !input.bad() && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0) {
            constexpr char digits[] = "0123456789abcdef";
            for (auto byte : digest) { hex += digits[byte >> 4]; hex += digits[byte & 15]; }
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return hex;
}
inline bool Install(std::uintptr_t base, FeatureHost::Side side) {
    auto profile = std::filesystem::path(ProfileRoot::Root()).lexically_normal();
    if (profile.filename().empty()) profile = profile.parent_path();
    wchar_t optIn[8]{};
    if (GetEnvironmentVariableW(L"DR_THRAX_TRACKING_PROBE", optIn, 8) != 1 || !Policy::QaProfile(profile.filename().wstring(), optIn)) return false;
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768 || HashFile(executable) != Policy::ExecutableSha256) return false;
    std::array<unsigned char, 32> bytes{};
    if (!Read(reinterpret_cast<void*>(base + Policy::CallbackRva), bytes) || bytes != Policy::Prologue) return false;
    // Initialize the SDK's immutable FName function pointer before worker calls;
    // otherwise its lazy initializer would write shared state during observation.
    if (!FName::AppendString) FName::InitInternal();
    ReportFile = L"thrax-tracking-probe-" + std::wstring(side == FeatureHost::Side::Client ? L"client-" : L"server-")
        + std::to_wstring(GetCurrentProcessId()) + L".jsonl";
    return Vibeless::Hook(base, Policy::CallbackRva, reinterpret_cast<void*>(&CallbackHook), reinterpret_cast<void**>(&Original),
        "ArchonSaveComponentBoneTransform.Observe", "thrax-tracking-probe");
}
template<size_t N> inline void ArrayJson(std::ostream& out, const std::array<float, N>& values) {
    out << '['; for (size_t index = 0; index < N; ++index) { if (index) out << ','; out << values[index]; } out << ']';
}
inline void TransformJson(std::ostream& out, const Policy::Transform& transform) {
    out << "{\"rotation\":"; ArrayJson(out, transform.Rotation);
    out << ",\"translation\":"; ArrayJson(out, transform.Translation);
    out << ",\"scale\":"; ArrayJson(out, transform.Scale); out << '}';
}
inline const char* RejectionName(Policy::Rejection value) {
    switch (value) {
    case Policy::Rejection::None: return "none";
    case Policy::Rejection::Layout: return "layout";
    case Policy::Rejection::Reflection: return "reflection";
    case Policy::Rejection::UnsupportedIndexMode: return "unsupported_compact_index_mode";
    case Policy::Rejection::BoneIndex: return "bone_index";
    case Policy::Rejection::ComponentSpace: return "component_space_not_materialized";
    case Policy::Rejection::Nonfinite: return "nonfinite";
    default: return "observer_exception";
    }
}
inline void Tick() {
    if (ReportFile.empty()) return;
    std::error_code error;
    const auto directory = std::filesystem::path(ProfileRoot::Root()) / L"reports";
    std::filesystem::create_directories(directory, error);
    if (error) return;
    std::ofstream out(directory / ReportFile, std::ios::app);
    if (!out) return;
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    if (!ReportStarted) {
        out << "{\"event\":\"observer_ready\",\"schema\":1,\"sampleLimit\":" << Policy::SampleLimit
            << ",\"observationOnly\":true,\"coherentUpdateStamp\":false,\"callbackRva\":\"0x12f1ec0\"}\n";
        ReportStarted = true;
    }
    const auto batch = Samples.Drain();
    for (size_t index = 0; index < batch.Count; ++index) {
        const auto& sample = batch.Samples[index];
        out << "{\"event\":\"callback_sample\",\"ordinal\":" << sample.Ordinal << ",\"thread\":" << sample.ThreadId
            << ",\"instanceIndex\":" << sample.InstanceIndex << ",\"nodeOffset\":" << sample.NodeOffset
            << ",\"classSize\":" << sample.ClassSize << ",\"propertyElementSize\":" << sample.PropertyElementSize
            << ",\"nativeStructSize\":" << sample.NativeStructSize
            << ",\"rejection\":\"" << RejectionName(sample.Rejected) << '"';
        // Failed reads may contain NaN or incomplete values: report only the reason.
        if (sample.Rejected == Policy::Rejection::None) {
            out << ",\"actualAlpha\":" << sample.ActualAlpha << ",\"lookAtAlpha\":" << sample.LookAtAlpha
                << ",\"curveAlpha\":" << sample.CurveAlpha << ",\"target\":"; ArrayJson(out, sample.Target);
            out << ",\"component\":"; TransformJson(out, sample.Component);
            out << ",\"bones\":[";
            for (size_t bone = 0; bone < Policy::BoneCount; ++bone) {
                if (bone) out << ',';
                out << "{\"name\":\"" << Policy::BoneNames[bone] << "\",\"compactIndex\":" << sample.CompactIndices[bone] << ",\"transform\":";
                TransformJson(out, sample.Bones[bone]); out << '}';
            }
            out << ']';
        }
        out << "}\n";
    }
}
}
