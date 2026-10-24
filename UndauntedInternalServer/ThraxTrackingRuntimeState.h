// Private tracking lifecycle; no engine pointers are dereferenced here. AGPL-3.0-only.
#pragma once
#include "ThraxTrackingState.h"
#include <algorithm>

namespace ThraxTrackingRuntime {
inline constexpr std::string_view AnimationClass = "/Game/Monsters/feareaterthrax144/animation/ml_randall_anim_bp.ml_randall_anim_bp_C";
inline constexpr std::string_view Marker = "Thrax144_Tracking_v1";
inline constexpr std::string_view NodeName = "AnimGraphNode_Thrax144_Tracking_Carrier";
inline constexpr float RelevanceThreshold = 1.e-5f;
using Quaternion = std::array<float, 4>;
inline constexpr Quaternion Identity{0, 0, 0, 1};
struct Owner {
    std::uintptr_t Instance = 0, Node = 0, Proxy = 0;
    std::int32_t Index = -1, Serial = 0;
    bool operator==(const Owner&) const = default;
    bool Valid() const { return Instance && Node && Proxy && Index >= 0 && Serial > 0; }
};
struct Token { Owner Identity; std::uint64_t Generation = 0; };
struct Ticket { Token Binding; std::uint64_t Epoch = 0; };
struct Traversal {
    std::uint64_t Frame = (std::numeric_limits<std::uint64_t>::max)();
    std::int16_t Counter = -1, SkippedFrames = 0;
    bool Valid() const { return Frame != (std::numeric_limits<std::uint64_t>::max)() && Counter != -1; }
    bool operator==(const Traversal& other) const { return Frame == other.Frame && Counter == other.Counter; }
};
struct Snapshot {
    std::array<float, 3> Target{};
    ThraxTracking::Frame Component;
    float Alpha = 0, Delta = 0;
    Traversal Stamp;
};
inline bool Finite(const Snapshot& value) {
    return ThraxTracking::Finite(value.Target) && ThraxTracking::Finite(value.Component.Rotation)
        && ThraxTracking::Finite(value.Component.Translation) && ThraxTracking::Finite(value.Component.Scale)
        && std::isfinite(value.Alpha) && std::isfinite(value.Delta) && value.Delta >= 0 && value.Stamp.Valid();
}
// Native callbacks supply an exact reflected class/marker/node and a serial-checked
// live Owner. Initialize replaces address reuse; destruction ends the generation.
// Evaluation executes the solver and output commit under the same lock as Update,
// so another callback cannot consume time twice or overwrite a newer history.
class State {
public:
    std::optional<Token> Initialize(const Owner& owner) {
        std::lock_guard lock(mutex_);
        if (!owner.Valid() || nextGeneration_ == (std::numeric_limits<std::uint64_t>::max)()
            || (entries_.size() >= 4096 && !entries_.contains(owner.Node))) return {};
        Entry entry; entry.Binding = {owner, ++nextGeneration_};
        entries_[owner.Node] = entry; return entry.Binding;
    }
    std::optional<Token> Find(const Owner& owner) {
        std::lock_guard lock(mutex_);
        const auto found = entries_.find(owner.Node);
        if (found == entries_.end() || found->second.Binding.Identity != owner) return {};
        return found->second.Binding;
    }
    std::optional<Token> BindingAt(std::uintptr_t node) {
        std::lock_guard lock(mutex_); const auto found = entries_.find(node);
        return found == entries_.end() ? std::optional<Token>{} : found->second.Binding;
    }
    void End(std::uintptr_t node) { std::lock_guard lock(mutex_); entries_.erase(node); }
    bool Reset(const Token& token) {
        std::lock_guard lock(mutex_); auto* entry = Lookup(token);
        if (!entry) return false;
        Clear(*entry); return true;
    }
    bool Reset(const Ticket& ticket) {
        std::lock_guard lock(mutex_); auto* entry = Lookup(ticket.Binding);
        if (!entry || entry->Epoch != ticket.Epoch) return false;
        Clear(*entry); return true;
    }
    bool Update(const Token& token, const Snapshot& snapshot) {
        std::lock_guard lock(mutex_); auto* entry = Lookup(token);
        if (!entry) return false;
        if (!Finite(snapshot) || entry->Epoch == (std::numeric_limits<std::uint64_t>::max)()) { Clear(*entry); return false; }
        ++entry->Epoch;
        if (snapshot.Alpha <= RelevanceThreshold) { Clear(*entry); return true; }
        const float delta = entry->Delta + snapshot.Delta;
        if (!std::isfinite(delta)) { Clear(*entry); return false; }
        entry->Value = snapshot; entry->Value.Delta = 0;
        entry->Delta = delta; entry->Ready = true; return true;
    }
    std::optional<Ticket> Prepare(const Token& token) {
        std::lock_guard lock(mutex_); auto* entry = Lookup(token);
        if (!entry || !entry->Ready) return {};
        return Ticket{token, entry->Epoch};
    }
    std::optional<Ticket> Prepare(const Token& token, const Traversal& stamp) {
        std::lock_guard lock(mutex_); auto* entry = Lookup(token);
        if (!entry || !entry->Ready || !stamp.Valid() || !(stamp == entry->Value.Stamp)) return {};
        return Ticket{token, entry->Epoch};
    }
    template<typename Commit> bool Evaluate(const Ticket& ticket, Commit&& commit) {
        std::lock_guard lock(mutex_); auto* entry = Lookup(ticket.Binding);
        if (!entry || !entry->Ready || !ticket.Epoch || ticket.Epoch != entry->Epoch) return false;
        auto next = entry->Previous;
        try {
            if (!commit(entry->Value, entry->Delta, entry->Previous, next, entry->Epoch)
                || !ThraxTracking::Finite(next)) { Clear(*entry); return false; }
        } catch (...) { Clear(*entry); return false; }
        entry->Previous = next; entry->Delta = 0; return true;
    }
    template<typename Commit> bool Evaluate(const Token& token, Commit&& commit) {
        const auto ticket = Prepare(token);
        return ticket && Evaluate(*ticket, std::forward<Commit>(commit));
    }
private:
    struct Entry {
        Token Binding;
        Snapshot Value;
        Quaternion Previous = Identity;
        float Delta = 0;
        std::uint64_t Epoch = 0;
        bool Ready = false;
    };
    Entry* Lookup(const Token& token) {
        const auto found = entries_.find(token.Identity.Node);
        return found != entries_.end() && found->second.Binding.Generation == token.Generation
            && token.Identity == found->second.Binding.Identity ? &found->second : nullptr;
    }
    static void Clear(Entry& entry) { entry.Value = {}; entry.Previous = Identity; entry.Delta = 0; entry.Ready = false; }
    std::mutex mutex_;
    std::map<std::uintptr_t, Entry> entries_;
    std::uint64_t nextGeneration_ = 0;
};
}
