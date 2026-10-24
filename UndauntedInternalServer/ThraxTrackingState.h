// Portable boundary for the private N1 tracking prototype. AGPL-3.0-only.
// No hooks or feature registration: engine lifecycle/snapshot publication must be
// independently qualified before wiring this state into evaluation callbacks.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <tuple>

namespace ThraxTracking {
inline constexpr std::string_view AnimationClass = "/Game/Monsters/feareaterthrax144/n1/ml_randall_anim_bp.ml_randall_anim_bp_C";
inline constexpr std::string_view Marker = "Thrax144_NativeTracking_v1";
inline constexpr std::string_view NodeName = "AnimGraphNode_Thrax144_N1_Carrier";

struct Eligibility {
    bool InProfile = false, FeatureEnabled = false;
    std::string_view ClassPath, PoseMarker, ReflectedNode;
};
inline bool Eligible(const Eligibility& value) {
    return value.InProfile && value.FeatureEnabled && value.ClassPath == AnimationClass
        && value.PoseMarker == Marker && value.ReflectedNode == NodeName;
}
struct Key {
    std::uintptr_t Instance = 0, Node = 0;
    bool operator<(const Key& other) const { return std::tie(Instance, Node) < std::tie(other.Instance, other.Node); }
};
struct Token { Key Owner; std::uint64_t Generation = 0; };
struct Frame {
    std::array<float, 4> Rotation{0, 0, 0, 1};
    std::array<float, 3> Translation{}, Scale{1, 1, 1};
};
struct Input {
    std::array<float, 3> Target{};
    Frame Component;
    float Alpha = 0, DeltaSeconds = 0;
    bool Relevant = false;
};
struct Evaluation {
    Input Snapshot;
    std::uint64_t Epoch = 0, Generation = 0, ResetRevision = 0;
    float AccumulatedDelta = 0;
};
template<size_t N> inline bool Finite(const std::array<float, N>& values) {
    for (const float value : values) if (!std::isfinite(value)) return false;
    return true;
}
inline bool Finite(const Input& value) {
    return Finite(value.Target) && Finite(value.Component.Rotation) && Finite(value.Component.Translation)
        && Finite(value.Component.Scale) && std::isfinite(value.Alpha) && std::isfinite(value.DeltaSeconds);
}

// All values cross the boundary by copy. No UObject/pose pointer is dereferenced.
// A caller must use a fresh Initialize token on every native initialization and
// End on teardown; address equality and the generated SDK weak pointer are not a
// substitute for those lifecycle events. ResetRevision invalidates solver history.
class State {
public:
    std::optional<Token> Initialize(Key key, const Eligibility& eligibility) {
        std::lock_guard lock(mutex_);
        if (!key.Instance || !key.Node || !Eligible(eligibility) || generation_ == (std::numeric_limits<std::uint64_t>::max)()) return {};
        Entry entry;
        entry.Generation = ++generation_;
        entries_[key] = entry;
        return Token{key, entry.Generation};
    }
    bool End(const Token& token) {
        std::lock_guard lock(mutex_);
        const auto found = Find(token);
        if (found == entries_.end()) return false;
        entries_.erase(found);
        return true;
    }
    bool Reset(const Token& token) {
        std::lock_guard lock(mutex_);
        const auto found = Find(token);
        if (found == entries_.end()) return false;
        Clear(found->second);
        return true;
    }
    bool Update(const Token& token, const Eligibility& eligibility, std::uint64_t epoch, const Input& input) {
        std::lock_guard lock(mutex_);
        const auto found = Find(token);
        if (found == entries_.end()) return false;
        auto& entry = found->second;
        // Stale publication cannot invalidate a newer snapshot.
        if (!epoch || epoch <= entry.Epoch) return false;
        entry.Epoch = epoch;
        if (!Eligible(eligibility) || !Finite(input)) { Clear(entry); return false; }
        if (!input.Relevant || !entry.Ready || !entry.Snapshot.Relevant) Clear(entry);
        const float accumulated = input.Relevant ? entry.AccumulatedDelta + input.DeltaSeconds : 0.0f;
        if (!std::isfinite(accumulated)) { Clear(entry); return false; }
        entry.Snapshot = input;
        entry.AccumulatedDelta = accumulated;
        entry.Ready = true;
        return true;
    }
    std::optional<Evaluation> Consume(const Token& token, const Eligibility& eligibility, std::uint64_t epoch) {
        std::lock_guard lock(mutex_);
        const auto found = Find(token);
        if (found == entries_.end()) return {};
        auto& entry = found->second;
        if (!Eligible(eligibility)) { Clear(entry); return {}; }
        if (!entry.Ready || !epoch || epoch != entry.Epoch) return {};
        Evaluation result{entry.Snapshot, entry.Epoch, entry.Generation, entry.ResetRevision, entry.AccumulatedDelta};
        entry.AccumulatedDelta = 0;
        // DeltaSeconds belongs to publication; only AccumulatedDelta is consumed
        // by the solver. Keeping it zero here prevents accidental double use.
        result.Snapshot.DeltaSeconds = 0;
        return result;
    }
private:
    struct Entry {
        Input Snapshot;
        std::uint64_t Generation = 0, Epoch = 0, ResetRevision = 0;
        float AccumulatedDelta = 0;
        bool Ready = false;
    };
    using Entries = std::map<Key, Entry>;
    Entries::iterator Find(const Token& token) {
        const auto found = entries_.find(token.Owner);
        return found != entries_.end() && token.Generation && found->second.Generation == token.Generation ? found : entries_.end();
    }
    static void Clear(Entry& entry) {
        entry.Snapshot = {};
        entry.AccumulatedDelta = 0;
        entry.Ready = false;
        ++entry.ResetRevision;
    }
    std::mutex mutex_;
    Entries entries_;
    std::uint64_t generation_ = 0;
};
}
