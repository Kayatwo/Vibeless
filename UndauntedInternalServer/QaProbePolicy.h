// QA engine probe rules: the request file, the motion verdict and the report (QaProbe.h is the engine
// side). Portable, so CI tests it on every host. AGPL-3.0-only.
//
// The probe runs only for launcher profiles named qa-*, on a dedicated server the launcher starts for
// it (`vibeless probe`). The request names the creature class, an optional stock control class and the
// package objects to load. The report is rewritten before and after every stage, so a crash or stall
// leaves the stage and asset in progress.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace QaProbePolicy {
inline constexpr int MaxAssets = 4096, MinSeconds = 2, MaxSeconds = 120, MaxBones = 512, MaxMontages = 32;
inline constexpr size_t MaxRequestBytes = 1 << 20, MaxPath = 512;
// A bone that never moves this far (cm, component space) from its first sample is still.
inline constexpr double StillCm = 0.5;

inline std::string Trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}

// The profile root's own folder name starts with "qa-" (root may end with a separator).
inline bool IsQaProfile(std::wstring_view root) {
    while (!root.empty() && (root.back() == L'/' || root.back() == L'\\')) root.remove_suffix(1);
    const auto slash = root.find_last_of(L"/\\");
    const auto name = slash == std::wstring_view::npos ? root : root.substr(slash + 1);
    return name.size() > 3 && name.substr(0, 3) == L"qa-";
}

inline bool ValidId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (const char c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

// /Game/<path>.<object>: printable ASCII, no spaces, no "..", one object separator after the last '/'.
inline bool ValidObjectPath(std::string_view path) {
    if (path.size() < 8 || path.size() > MaxPath || path.substr(0, 6) != "/Game/" || path.find("..") != std::string_view::npos) return false;
    for (const char c : path) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '.' || c == '+';
        if (!ok) return false;
    }
    const auto slash = path.find_last_of('/'), dot = path.find_last_of('.');
    return dot != std::string_view::npos && dot > slash + 1 && dot + 1 < path.size();
}
inline bool ValidClassPath(std::string_view path) {
    return ValidObjectPath(path) && path.size() > 2 && path.substr(path.size() - 2) == "_C";
}

struct Request {
    std::string Id, Creature, Control;
    std::vector<std::string> Assets;
    int SampleSeconds = 20;
};

// Key=value lines; '#' starts a comment. Version, Id and Creature are required; Asset repeats.
inline std::optional<Request> Parse(std::string_view text, std::string& error) {
    if (text.size() > MaxRequestBytes) { error = "request is larger than 1 MiB"; return std::nullopt; }
    Request request;
    std::set<std::string> seen, assets;
    bool version = false;
    size_t start = 0;
    while (start <= text.size()) {
        const auto end = (std::min)(text.find('\n', start), text.size());
        const auto line = Trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == '#') continue;
        const auto split = line.find('=');
        if (split == std::string::npos) { error = "line without '=': " + line.substr(0, 64); return std::nullopt; }
        const auto key = Trim(line.substr(0, split)), value = Trim(line.substr(split + 1));
        if (key != "Asset" && !seen.insert(key).second) { error = key + " is given twice"; return std::nullopt; }
        if (key == "Version") {
            if (value != "1") { error = "unsupported request version " + value.substr(0, 16); return std::nullopt; }
            version = true;
        } else if (key == "Id") {
            if (!ValidId(value)) { error = "invalid Id"; return std::nullopt; }
            request.Id = value;
        } else if (key == "Creature" || key == "Control") {
            if (!ValidClassPath(value)) { error = "invalid " + key + " class path"; return std::nullopt; }
            (key == "Creature" ? request.Creature : request.Control) = value;
        } else if (key == "SampleSeconds") {
            int seconds = 0;
            if (value.empty() || value.size() > 3) { error = "invalid SampleSeconds"; return std::nullopt; }
            for (const char c : value) {
                if (c < '0' || c > '9') { error = "invalid SampleSeconds"; return std::nullopt; }
                seconds = seconds * 10 + (c - '0');
            }
            if (seconds < MinSeconds || seconds > MaxSeconds) { error = "SampleSeconds must be 2-120"; return std::nullopt; }
            request.SampleSeconds = seconds;
        } else if (key == "Asset") {
            if (!ValidObjectPath(value)) { error = "invalid Asset path: " + value.substr(0, 64); return std::nullopt; }
            if (!assets.insert(value).second) { error = "Asset is listed twice: " + value.substr(0, 64); return std::nullopt; }
            if (assets.size() > static_cast<size_t>(MaxAssets)) { error = "more than 4096 assets"; return std::nullopt; }
            request.Assets.push_back(value);
        } else { error = "unknown key " + key.substr(0, 32); return std::nullopt; }
    }
    if (!version || request.Id.empty() || request.Creature.empty()) { error = "Version, Id and Creature are required"; return std::nullopt; }
    if (request.Control == request.Creature) { error = "Control must differ from Creature"; return std::nullopt; }
    return request;
}

// Component-space bone positions over time. Bone 0 (the root) is excluded by the caller's indices.
struct Vec { double X = 0, Y = 0, Z = 0; };
class MotionTrack {
public:
    // One sample: positions of the same bones, in the same order, every time.
    bool Add(const std::vector<Vec>& bones) {
        if (bones.empty() || bones.size() > static_cast<size_t>(MaxBones)) return false;
        for (const auto& b : bones) if (!std::isfinite(b.X) || !std::isfinite(b.Y) || !std::isfinite(b.Z)) { ++invalid_; return false; }
        if (first_.empty()) { first_ = bones; delta_.assign(bones.size(), 0); }
        else if (bones.size() != first_.size()) return false;
        for (size_t i = 0; i < bones.size(); ++i) {
            const double dx = bones[i].X - first_[i].X, dy = bones[i].Y - first_[i].Y, dz = bones[i].Z - first_[i].Z;
            delta_[i] = (std::max)(delta_[i], std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        ++samples_;
        return true;
    }
    int Samples() const { return samples_; }
    int Invalid() const { return invalid_; }
    int Bones() const { return static_cast<int>(first_.size()); }
    int MovingBones() const { int n = 0; for (const double d : delta_) n += d > StillCm; return n; }
    double MaxDeltaCm() const { double m = 0; for (const double d : delta_) m = (std::max)(m, d); return m; }
private:
    std::vector<Vec> first_;
    std::vector<double> delta_;
    int samples_ = 0, invalid_ = 0;
};

enum class Motion { NoSamples, Moving, Frozen, Inconclusive };
inline const char* Name(Motion motion) {
    switch (motion) {
        case Motion::Moving: return "moving";
        case Motion::Frozen: return "frozen-pose";
        case Motion::Inconclusive: return "inconclusive";
        default: return "no-samples";
    }
}
// A still creature is only called frozen when a stock control in the same server moved; otherwise the
// server may simply not have animated anything.
inline Motion Judge(const MotionTrack& creature, const MotionTrack* control) {
    if (creature.Samples() < 2) return Motion::NoSamples;
    if (creature.MovingBones() > 0) return Motion::Moving;
    if (control && control->Samples() >= 2 && control->MovingBones() > 0) return Motion::Frozen;
    return Motion::Inconclusive;
}

// ---- Report ---------------------------------------------------------------------------------
inline std::string Quote(std::string_view text) {
    std::string out = "\"";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20) { static const char hex[] = "0123456789abcdef"; out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + "\"";
}
inline std::string Number(double value) {
    if (!std::isfinite(value)) return "null";
    char buffer[400];  // any finite double in %.3f fits
    std::snprintf(buffer, sizeof buffer, "%.3f", value);
    return buffer;
}

struct Stage { std::string Name, Status, Detail; double Seconds = 0; };  // Status: running, ok, failed
struct Subject {
    std::string Role, Class, AnimClass;
    bool Spawned = false;
    double Health = 0, MaxHealth = 0, MovedCm = 0;
    std::vector<std::string> Montages;
    MotionTrack Track;
    void AddMontage(const std::string& montage) {
        if (montage.empty() || Montages.size() >= static_cast<size_t>(MaxMontages)) return;
        for (const auto& m : Montages) if (m == montage) return;
        Montages.push_back(montage);
    }
};
struct Report {
    std::string Id, Creature, Control, Map, Runtime, Verdict = "running", Reason, CurrentAsset;
    int Port = 0;
    unsigned long Pid = 0;
    int AssetsRequested = 0, AssetsLoaded = 0, AssetsAsClass = 0;  // AsClass: Blueprints loaded as <name>_C
    std::vector<std::string> FailedAssets;
    std::vector<Stage> Stages;
    std::vector<Subject> Subjects;
    bool Finished = false;

    Stage& Begin(const std::string& name) { Stages.push_back({name, "running", {}, 0}); return Stages.back(); }
    // The first failing stage decides the verdict; later stages still run when they can.
    void Fail(Stage& stage, const std::string& reason) {
        stage.Status = "failed"; stage.Detail = reason;
        if (Verdict == "running") { Verdict = "fail-" + stage.Name; Reason = reason; }
    }

    std::string ToJson() const {
        std::string out = "{\n  \"schema\": 1,\n  \"id\": " + Quote(Id) + ",\n  \"verdict\": " + Quote(Verdict)
            + ",\n  \"reason\": " + Quote(Reason) + ",\n  \"finished\": " + (Finished ? "true" : "false")
            + ",\n  \"creature\": " + Quote(Creature) + ",\n  \"control\": " + Quote(Control)
            + ",\n  \"server\": {\"port\": " + std::to_string(Port) + ", \"pid\": " + std::to_string(Pid)
            + ", \"map\": " + Quote(Map) + ", \"runtime\": " + Quote(Runtime) + "},\n  \"stages\": [";
        for (size_t i = 0; i < Stages.size(); ++i) {
            const auto& s = Stages[i];
            out += std::string(i ? "," : "") + "\n    {\"name\": " + Quote(s.Name) + ", \"status\": " + Quote(s.Status)
                + ", \"seconds\": " + Number(s.Seconds) + (s.Detail.empty() ? std::string() : ", \"detail\": " + Quote(s.Detail)) + "}";
        }
        out += std::string(Stages.empty() ? "" : "\n  ") + "],\n  \"load\": {\"requested\": " + std::to_string(AssetsRequested)
            + ", \"loaded\": " + std::to_string(AssetsLoaded) + ", \"asClass\": " + std::to_string(AssetsAsClass)
            + ", \"current\": " + Quote(CurrentAsset) + ", \"failed\": [";
        for (size_t i = 0; i < FailedAssets.size(); ++i) out += std::string(i ? ", " : "") + Quote(FailedAssets[i]);
        out += "]},\n  \"subjects\": [";
        for (size_t i = 0; i < Subjects.size(); ++i) {
            const auto& s = Subjects[i];
            const MotionTrack* control = nullptr;
            for (const auto& other : Subjects) if (other.Role == "control") control = &other.Track;
            out += std::string(i ? "," : "") + "\n    {\"role\": " + Quote(s.Role) + ", \"class\": " + Quote(s.Class)
                + ", \"spawned\": " + (s.Spawned ? "true" : "false") + ", \"animClass\": " + Quote(s.AnimClass)
                + ", \"health\": " + Number(s.Health) + ", \"maxHealth\": " + Number(s.MaxHealth) + ", \"movedCm\": " + Number(s.MovedCm)
                + ", \"motion\": {\"verdict\": " + Quote(Name(Judge(s.Track, s.Role == "control" ? nullptr : control)))
                + ", \"samples\": " + std::to_string(s.Track.Samples()) + ", \"bones\": " + std::to_string(s.Track.Bones())
                + ", \"movingBones\": " + std::to_string(s.Track.MovingBones()) + ", \"maxDeltaCm\": " + Number(s.Track.MaxDeltaCm())
                + ", \"invalidSamples\": " + std::to_string(s.Track.Invalid()) + "}, \"montages\": [";
            for (size_t m = 0; m < s.Montages.size(); ++m) out += std::string(m ? ", " : "") + Quote(s.Montages[m]);
            out += "]}";
        }
        return out + std::string(Subjects.empty() ? "" : "\n  ") + "]\n}\n";
    }
};
}
