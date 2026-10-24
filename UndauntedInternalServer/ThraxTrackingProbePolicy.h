// Bounded read-only N1 observation policy and measured 1.4.4 ABI. AGPL-3.0-only.
#pragma once
#include "ThraxTrackingState.h"
#include <cstddef>
#include <cstring>
#include <utility>

namespace ThraxTrackingProbePolicy {
inline constexpr size_t BoneCount = 7, SampleLimit = 16;
inline constexpr std::uintptr_t CallbackRva = 0x12f1ec0;
inline constexpr std::string_view ExecutableSha256 = "d3d41e614908d2befd518b27046d9822d6130ef12ba3504babbdb786bef9cff4";
inline constexpr std::array<unsigned char, 32> Prologue{
    0x40,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x48,0x83,0xec,0x30,0x48,0x8b,0x3a,0x4c,
    0x8b,0xe2,0x48,0x8b,0xf1,0x48,0x8b,0x9f,0xa0,0x00,0x00,0x00,0x48,0x85,0xdb,0x74};
inline constexpr std::array<std::string_view, BoneCount> BoneNames{
    "neck_00_jnt", "neck_01_jnt", "neck_02_jnt", "neck_03_jnt", "neck_04_jnt", "neck_05_jnt", "head_jnt"};
inline bool QaProfile(std::wstring_view name, std::wstring_view optIn) {
    return optIn == L"1" && name == L"qa-probe";
}
template<typename T> struct Array { T* Data = nullptr; std::int32_t Count = 0, Capacity = 0; };
struct alignas(16) Transform {
    std::array<float, 4> Rotation{};
    std::array<float, 3> Translation{}; float TranslationPadding = 0;
    std::array<float, 3> Scale{}; float ScalePadding = 0;
};
struct BoneReference { std::uint64_t Name = 0; std::int32_t IndexMode = 0, CompactIndex = 0; };
struct Node {
    std::array<std::byte, 0x24> Prefix{};
    float ActualAlpha = 0;
    std::array<std::byte, 0xa0> Padding{};
    std::uint64_t PoseName = 0;
    Array<BoneReference> Bones;
};
struct Context {
    void* Proxy = nullptr;
    Array<Transform> Pose;
    void* BoneContainer = nullptr;
    Array<std::uint8_t> Flags;
};
static_assert(sizeof(void*) == 8);
static_assert(sizeof(Transform) == 0x30 && alignof(Transform) == 16);
static_assert(offsetof(Transform, Translation) == 0x10 && offsetof(Transform, Scale) == 0x20);
static_assert(sizeof(Array<Transform>) == 0x10 && sizeof(BoneReference) == 0x10);
static_assert(offsetof(BoneReference, IndexMode) == 8 && offsetof(BoneReference, CompactIndex) == 12);
static_assert(sizeof(Node) == 0xe0 && offsetof(Node, ActualAlpha) == 0x24);
static_assert(offsetof(Node, PoseName) == 0xc8 && offsetof(Node, Bones) == 0xd0);
static_assert(sizeof(Context) == 0x30 && offsetof(Context, Pose) == 8);
static_assert(offsetof(Context, BoneContainer) == 0x18 && offsetof(Context, Flags) == 0x20);
inline bool Finite(const Transform& transform) {
    return ThraxTracking::Finite(transform.Rotation) && ThraxTracking::Finite(transform.Translation) && ThraxTracking::Finite(transform.Scale);
}
template<typename T> inline bool ValidArray(const Array<T>& value, std::int32_t limit = 4096) {
    return value.Data && value.Count > 0 && value.Count <= limit && value.Capacity >= value.Count && value.Capacity <= 65536;
}
inline bool ValidIndex(const BoneReference& bone, const Context& context) {
    return bone.IndexMode >= 0 && bone.CompactIndex >= 0 && ValidArray(context.Pose) && ValidArray(context.Flags)
        && context.Pose.Count == context.Flags.Count && bone.CompactIndex < context.Pose.Count;
}
enum class Rejection { None, Layout, Reflection, UnsupportedIndexMode, BoneIndex, ComponentSpace, Nonfinite, Exception };
struct Sample {
    std::uint32_t Ordinal = 0, ThreadId = 0;
    std::int32_t InstanceIndex = -1, NodeOffset = 0;
    std::int32_t ClassSize = 0, PropertyElementSize = 0, NativeStructSize = 0;
    Rejection Rejected = Rejection::None;
    float ActualAlpha = 0, LookAtAlpha = 0, CurveAlpha = 0;
    std::array<float, 3> Target{};
    std::array<std::uint32_t, 2> AlignmentPadding{};
    Transform Component;
    std::array<std::int32_t, BoneCount> CompactIndices{};
    std::uint32_t BoneAlignmentPadding = 0;
    std::array<Transform, BoneCount> Bones{};
};
// A lifetime budget, not merely a bounded pending queue: draining never permits
// unbounded captures. All records contain copied numbers, never engine pointers.
class Queue {
public:
    bool Full() { std::lock_guard lock(mutex_); return reserved_ == SampleLimit; }
    std::optional<std::uint32_t> Reserve() {
        std::lock_guard lock(mutex_);
        if (reserved_ == SampleLimit) return {};
        return static_cast<std::uint32_t>(++reserved_);
    }
    void Publish(Sample sample) {
        std::lock_guard lock(mutex_);
        if (!sample.Ordinal || sample.Ordinal > reserved_ || published_[sample.Ordinal - 1]) return;
        published_[sample.Ordinal - 1] = true;
        pending_[count_++] = sample;
    }
    struct Batch { std::array<Sample, SampleLimit> Samples{}; size_t Count = 0, AlignmentPadding = 0; };
    Batch Drain() {
        std::lock_guard lock(mutex_);
        Batch batch{pending_, count_}; count_ = 0; return batch;
    }
private:
    std::mutex mutex_;
    size_t reserved_ = 0, count_ = 0;
    std::array<bool, SampleLimit> published_{};
    std::array<Sample, SampleLimit> pending_{};
};
template<typename Original, typename Observer> inline void AfterOriginal(Original&& original, Observer&& observer) {
    std::forward<Original>(original)();
    try { std::forward<Observer>(observer)(); } catch (...) { /* Never change engine control flow for a failed diagnostic. */ }
}
}
