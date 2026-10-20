// Vibeless feature host: one owner for every native hook, lifecycle events dispatched to the
// features a profile enables, and a startup report naming each feature's state. AGPL-3.0-only.
//
// The core runtime owns the hooks. Features never call MinHook for an address the core uses;
// they subscribe to events instead, and any hook a feature does need is claimed here first, so
// two owners can never race to replace the same function.
//
// Enable list: DR_FEATURES, set per profile by the launcher or service runner.
//   unset        every compiled feature whose DefaultEnabled is true (the behaviour before Phase 2)
//   none         no features
//   a,b,c        these feature IDs and the features they require (comma or space separated)
// Package manifests name the features they need as capabilities ("native-feature:deepfrost"); the
// launcher passes those IDs here. A feature whose requirement failed, or whose Install fails, stays
// off; the report says why.
// Features run only inside a Vibeless profile (ProfileRoot); outside one they are all off.
//
// This header is portable (no Windows or SDK types) so the rules are tested on every CI host.
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace FeatureHost {
inline constexpr int ApiVersion = 1;

enum class Side : unsigned { Client = 1, Server = 2, Both = 3 };
inline bool Includes(Side sides, Side side) {
    return (static_cast<unsigned>(sides) & static_cast<unsigned>(side)) != 0;
}
inline const char* Name(Side side) {
    return side == Side::Client ? "client" : side == Side::Server ? "server" : "both";
}

// Event payloads stay opaque here; the runtime passes UWorld*, UNetDriver*, UObject*, UFunction*.
struct Tick { void* World = nullptr; void* Driver = nullptr; int Port = 0; float DeltaSeconds = 0; };
struct Event { void* Object = nullptr; void* Function = nullptr; void* Parms = nullptr; };

// Lifecycle events, in the order the runtime raises them during one frame.
//   ClientTickBefore/After      around the client's engine tick (client only)
//   ServerTick                  after networking has ticked on a listening game server
//   ClientIntercept             client ProcessEvent, before the original; true skips the original
//   ClientAfter                 client ProcessEvent, after the original
//   ServerIntercept             server ProcessEvent, first of all; true skips the original
//   ServerBeforeOriginal        server ProcessEvent, after the core's RPC guard, before the original
//   ServerAfter                 server ProcessEvent, after the original
struct Feature {
    std::string Id;
    std::string Version;
    Side Sides = Side::Both;
    bool DefaultEnabled = true;
    std::vector<std::string> Requires;
    std::function<bool(Side)> Install;  // optional; false keeps the feature off
    std::function<void(const Tick&)> ClientTickBefore, ClientTickAfter, ServerTick;
    std::function<bool(const Event&)> ClientIntercept, ServerIntercept;
    std::function<void(const Event&)> ClientAfter, ServerBeforeOriginal, ServerAfter;
};

struct HookClaim { std::uintptr_t Target = 0; std::string Name, Owner; bool Active = false; std::string Error; };

struct FeatureState { std::string Id, Version; bool Enabled = false; std::string Reason; };

struct EnableList {
    bool Defaults = true;               // unset: use each feature's DefaultEnabled
    std::set<std::string> Ids;          // explicit list (empty with Defaults=false means none)
};

inline std::string Trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

inline EnableList ParseEnableList(std::optional<std::string_view> text) {
    EnableList list;
    if (!text) return list;
    const auto value = Trim(*text);
    list.Defaults = false;
    if (value.empty() || value == "none") return list;
    std::string token;
    for (const char c : value + ",") {
        if (c == ',' || c == ' ' || c == '\t') {
            if (!token.empty()) list.Ids.insert(token);
            token.clear();
        } else {
            token += c;
        }
    }
    return list;
}

class Host {
public:
    // Registration order is dispatch order; a feature's requirements must be registered before it.
    // Returns an error message, or empty on success.
    std::string Register(Feature feature) {
        if (started_) return "feature host already started: " + feature.Id;
        if (feature.Id.empty()) return "feature without an ID";
        for (const auto& existing : features_)
            if (existing.Id == feature.Id) return "feature " + feature.Id + " is registered twice";
        for (const auto& requirement : feature.Requires) {
            const bool earlier = std::any_of(features_.begin(), features_.end(),
                [&](const Feature& f) { return f.Id == requirement; });
            if (!earlier) return "feature " + feature.Id + " requires " + requirement + ", which is not registered before it";
        }
        features_.push_back(std::move(feature));
        return {};
    }

    // Decides which features run on this side, installs them in order and builds the dispatch lists.
    void Start(Side side, bool inProfile, const EnableList& list) {
        if (started_) return;
        started_ = true;
        side_ = side;
        std::set<std::string> enabled;
        std::map<std::string, std::string> requiredBy;
        if (!list.Defaults) {
            // Requirements are registered before their dependents, so one backward pass closes the list.
            std::set<std::string> wanted = list.Ids;
            for (auto it = features_.rbegin(); it != features_.rend(); ++it)
                if (wanted.count(it->Id))
                    for (const auto& requirement : it->Requires)
                        if (wanted.insert(requirement).second) requiredBy[requirement] = it->Id;
            listed_ = std::move(wanted);
        }
        for (const auto& id : list.Ids) {
            const bool known = std::any_of(features_.begin(), features_.end(), [&](const Feature& f) { return f.Id == id; });
            if (!known) errors_.push_back("DR_FEATURES names unknown feature " + id);
        }
        for (auto& feature : features_) {
            FeatureState state{feature.Id, feature.Version, false, {}};
            const bool wanted = list.Defaults ? feature.DefaultEnabled : listed_.count(feature.Id) > 0;
            const auto missing = std::find_if(feature.Requires.begin(), feature.Requires.end(),
                [&](const std::string& id) { return !enabled.count(id); });
            if (!inProfile) state.Reason = "not in a Vibeless profile";
            else if (!wanted) state.Reason = list.Defaults ? "off by default" : "not in DR_FEATURES";
            else if (!Includes(feature.Sides, side)) state.Reason = std::string("not used on the ") + Name(side);
            else if (missing != feature.Requires.end()) state.Reason = "requires " + *missing + ", which is off";
            else if (feature.Install && !feature.Install(side)) state.Reason = "install failed";
            else {
                state.Enabled = true;
                enabled.insert(feature.Id);
                if (requiredBy.count(feature.Id)) state.Reason = "required by " + requiredBy[feature.Id];
                Subscribe(feature);
            }
            states_.push_back(state);
        }
    }

    // Records an owner for a hook address. Returns an error naming both owners on a clash.
    std::string Claim(std::uintptr_t target, std::string_view name, std::string_view owner) {
        for (const auto& claim : hooks_)
            if (claim.Target == target)
                return "hook " + std::string(name) + " is owned by " + claim.Owner + "; " + std::string(owner) + " cannot also hook it";
        hooks_.push_back({target, std::string(name), std::string(owner), false, {}});
        return {};
    }
    void MarkHook(std::uintptr_t target, bool active, std::string error = {}) {
        for (auto& claim : hooks_)
            if (claim.Target == target) { claim.Active = active; claim.Error = std::move(error); }
    }
    void AddError(std::string error) { errors_.push_back(std::move(error)); }

    bool Enabled(std::string_view id) const {
        return std::any_of(states_.begin(), states_.end(), [&](const FeatureState& s) { return s.Id == id && s.Enabled; });
    }
    bool HasClientTick() const { return !clientTickBefore_.empty() || !clientTickAfter_.empty(); }

    void ClientTickBefore(const Tick& tick) const { for (const auto& f : clientTickBefore_) f(tick); }
    void ClientTickAfter(const Tick& tick) const { for (const auto& f : clientTickAfter_) f(tick); }
    void ServerTick(const Tick& tick) const { for (const auto& f : serverTick_) f(tick); }
    bool ClientIntercept(const Event& event) const {
        for (const auto& f : clientIntercept_) if (f(event)) return true;
        return false;
    }
    void ClientAfter(const Event& event) const { for (const auto& f : clientAfter_) f(event); }
    bool ServerIntercept(const Event& event) const {
        for (const auto& f : serverIntercept_) if (f(event)) return true;
        return false;
    }
    void ServerBeforeOriginal(const Event& event) const { for (const auto& f : serverBefore_) f(event); }
    void ServerAfter(const Event& event) const { for (const auto& f : serverAfter_) f(event); }

    const std::vector<FeatureState>& States() const { return states_; }
    const std::vector<HookClaim>& Hooks() const { return hooks_; }
    const std::vector<std::string>& Errors() const { return errors_; }

    // Startup report: no account, character or network identifiers.
    std::string Report(std::string_view runtimeVersion, int port) const {
        std::string out = "{\n  \"schema\": 1,\n  \"api\": " + std::to_string(ApiVersion)
            + ",\n  \"runtime\": " + Quote(runtimeVersion) + ",\n  \"side\": " + Quote(Name(side_))
            + ",\n  \"port\": " + std::to_string(port) + ",\n  \"features\": [";
        for (size_t i = 0; i < states_.size(); ++i) {
            const auto& s = states_[i];
            out += std::string(i ? "," : "") + "\n    {\"id\": " + Quote(s.Id) + ", \"version\": " + Quote(s.Version)
                + ", \"enabled\": " + (s.Enabled ? "true" : "false")
                + (s.Reason.empty() ? std::string() : ", \"reason\": " + Quote(s.Reason)) + "}";
        }
        out += std::string(states_.empty() ? "" : "\n  ") + "],\n  \"hooks\": [";
        for (size_t i = 0; i < hooks_.size(); ++i) {
            const auto& h = hooks_[i];
            out += std::string(i ? "," : "") + "\n    {\"name\": " + Quote(h.Name) + ", \"owner\": " + Quote(h.Owner)
                + ", \"active\": " + (h.Active ? "true" : "false")
                + (h.Error.empty() ? std::string() : ", \"error\": " + Quote(h.Error)) + "}";
        }
        out += std::string(hooks_.empty() ? "" : "\n  ") + "],\n  \"errors\": [";
        for (size_t i = 0; i < errors_.size(); ++i) out += std::string(i ? ", " : "") + Quote(errors_[i]);
        return out + "]\n}\n";
    }

    static std::string Quote(std::string_view text) {
        std::string out = "\"";
        for (const unsigned char c : text) {
            if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
            else if (c < 0x20) { static const char hex[] = "0123456789abcdef"; out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
            else out += static_cast<char>(c);
        }
        return out + "\"";
    }

private:
    void Subscribe(const Feature& f) {
        if (f.ClientTickBefore) clientTickBefore_.push_back(f.ClientTickBefore);
        if (f.ClientTickAfter) clientTickAfter_.push_back(f.ClientTickAfter);
        if (f.ServerTick) serverTick_.push_back(f.ServerTick);
        if (f.ClientIntercept) clientIntercept_.push_back(f.ClientIntercept);
        if (f.ClientAfter) clientAfter_.push_back(f.ClientAfter);
        if (f.ServerIntercept) serverIntercept_.push_back(f.ServerIntercept);
        if (f.ServerBeforeOriginal) serverBefore_.push_back(f.ServerBeforeOriginal);
        if (f.ServerAfter) serverAfter_.push_back(f.ServerAfter);
    }

    bool started_ = false;
    Side side_ = Side::Client;
    std::vector<Feature> features_;
    std::set<std::string> listed_;
    std::vector<FeatureState> states_;
    std::vector<HookClaim> hooks_;
    std::vector<std::string> errors_;
    std::vector<std::function<void(const Tick&)>> clientTickBefore_, clientTickAfter_, serverTick_;
    std::vector<std::function<bool(const Event&)>> clientIntercept_, serverIntercept_;
    std::vector<std::function<void(const Event&)>> clientAfter_, serverBefore_, serverAfter_;
};
}
