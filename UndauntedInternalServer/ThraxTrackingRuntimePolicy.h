// Measured 1.4.4 adapter ABI and portable output contract. AGPL-3.0-only.
#pragma once
#include "ThraxTrackingRuntimeState.h"
#include "ThraxTrackingProbePolicy.h"

namespace ThraxTrackingRuntime {
namespace Abi = ThraxTrackingProbePolicy;
inline constexpr std::uintptr_t NodeVtableRva = 0x47c4dd0;
inline constexpr std::uintptr_t InitializeRva = 0xa78f20, UpdateRva = 0x2d4fda0;
inline constexpr std::uintptr_t DestroyRva = 0x19d32e0, WeakAssignRva = 0x1f77250, WeakGetRva = 0x1f79240;
inline constexpr size_t UpdateTraversalOffset = 0x418;
struct WeakHandle { std::int32_t Index = -1, Serial = 0; };
struct UpdateContext { void* Proxy = nullptr; void* Shared = nullptr; float Weight = 0, RootWeight = 0, Delta = 0; };
struct alignas(16) BoneTransform { std::int32_t Index = -1; std::array<std::byte, 12> Padding{}; Abi::Transform Value; };
static_assert(sizeof(WeakHandle) == 8 && offsetof(WeakHandle, Serial) == 4);
static_assert(sizeof(Traversal) == 16 && offsetof(Traversal, Counter) == 8 && offsetof(Traversal, SkippedFrames) == 10);
static_assert(offsetof(UpdateContext, Delta) == 0x18);
static_assert(sizeof(BoneTransform) == 0x40 && offsetof(BoneTransform, Value) == 0x10 && alignof(BoneTransform) == 16);
inline std::optional<float> EffectiveAlpha(float stockAlpha, float lookAt, float curve) {
    if (!std::isfinite(stockAlpha) || !std::isfinite(lookAt) || !std::isfinite(curve)) return {};
    if (stockAlpha <= RelevanceThreshold) return 0.f; // Includes the native LOD gate.
    const float product = lookAt * curve;
    if (!std::isfinite(product)) return {};
    return std::clamp(product, 0.f, 1.f);
}
inline bool OrderedChain(const std::array<std::int32_t, 7>& indices, const std::array<std::int32_t, 7>& parents) {
    if (indices[0] < 0) return false;
    for (size_t i = 1; i < indices.size(); ++i)
        if (indices[i] <= indices[i - 1] || parents[i] != indices[i - 1]) return false;
    return true;
}
inline bool ValidOutput(const Abi::Transform& value) {
    if (!Abi::Finite(value)) return false;
    float norm = 0;
    for (float component : value.Rotation) norm += component * component;
    return std::isfinite(norm) && std::fabs(norm - 1.f) < 2.e-4f;
}
// The engine owns this TArray allocation and frees it in its native destructor.
// No stack/CRT allocation can escape into it. Count is published after all seven
// transforms; the existing outer control evaluator performs the sole alpha blend.
template<typename Reallocate> inline bool PublishOutput(Abi::Array<BoneTransform>& output,
    const std::array<BoneTransform, 7>& transforms, Reallocate&& reallocate) {
    if (output.Count != 0 || output.Capacity < 0 || output.Capacity > 65536
        || (output.Capacity != 0 && !output.Data)) return false;
    for (size_t i = 0; i < transforms.size(); ++i)
        if (!ValidOutput(transforms[i].Value) || transforms[i].Index < 0
            || (i && transforms[i].Index <= transforms[i - 1].Index)) return false;
    if (output.Capacity < 7) {
        auto* allocation = static_cast<BoneTransform*>(reallocate(output.Data, sizeof(BoneTransform) * 7, alignof(BoneTransform)));
        if (!allocation) return false;
        output.Data = allocation; output.Capacity = 7;
    }
    std::memcpy(output.Data, transforms.data(), sizeof(transforms));
    output.Count = 7; return true;
}
}
