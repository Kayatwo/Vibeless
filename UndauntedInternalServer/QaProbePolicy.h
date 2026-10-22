// QA engine probe rules: the request file, the motion, streaming and portrait verdicts and the report
// (QaProbe.h is the server side, QaProbeClient.h the client side). Portable, so CI tests it on every
// host. AGPL-3.0-only.
//
// The probe runs only for launcher profiles named qa-*, in a dedicated server or a client the launcher
// starts for it (`vibeless probe [--client]`). The request names the creature class, an optional stock
// control class and the package objects to load. The report is rewritten before and after every stage,
// so a crash or stall leaves the stage and asset in progress.
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

// ---- Server: forced actions -----------------------------------------------------------------
inline constexpr int MaxActions = 64;
inline constexpr double ActionCapSeconds = 15;
// One row of the creature's action-set table, forced on the live server creature with its AI
// stopped: the row's montage played to its end. Notifies: Blueprint notifies on the creature's mesh
// (native notifies such as sounds do not pass through ProcessEvent). Spawned: actors the creature
// owned or instigated that appeared while it played.
struct ActionResult {
    std::string Row, Action, Montage;
    bool Played = false;
    double Length = 0, Seconds = 0;
    int Notifies = 0, Spawned = 0;
    std::string Role = "creature", Route = "table-montage", SelectedMontage = {}, Error = {};
    bool Observed = false, Completed = false;
};
// "" when every row played; otherwise why the actions stage fails.
inline std::string JudgeActions(const std::vector<ActionResult>& actions) {
    // A no-table-montage row needs real execution on both the creature and its stock control.
    // Both missing/idle is a coverage failure, never a passing comparison.
    for (const auto& a : actions) if (a.Route == "action-class") {
        if ((a.Role != "creature" && a.Role != "control") || a.Row.empty() || a.Action.empty())
            return "invalid action-class subject identity";
        if (!a.Error.empty()) return a.Row + " (" + a.Role + "): " + a.Error;
        if (!a.Played || !a.Observed || !a.Completed || a.SelectedMontage.empty())
            return a.Row + " (" + a.Role + "): action-class execution incomplete";
        {
            int peers = 0, sameRole = 0;
            for (const auto& b : actions)
                if (b.Route == a.Route && b.Row == a.Row && b.Action == a.Action) {
                    if (b.Role == a.Role) ++sameRole; else ++peers;
                }
            if (peers != 1 || sameRole != 1) return a.Row + ": exactly one creature and stock action-class control are required";
        }
    }
    int failed = 0;
    std::string first;
    for (const auto& a : actions) if (!a.Played) { if (!failed++) first = a.Row + (a.Montage.empty() ? " (no montage)" : " (" + a.Montage + ")"); }
    if (!failed) return {};
    return std::to_string(failed) + " of " + std::to_string(actions.size()) + " actions did not play, first " + first;
}

// ---- Server: part breaks and kill -----------------------------------------------------------
inline constexpr int MaxParts = 64;
// One monster part of a subject, before and after forced part damage (the part's own HandleDamage).
// Loot: the part's loot name for each break stage (GetBreakLootName), i.e. the rows it drops.
// Breakable: its break data has a next stage. CanBeBroken: the native flag at the time (false on
// stock parts outside a player's attack, so it is recorded, not required).
struct PartResult {
    std::string Role, Name;
    int Stages = 0, Before = 0, After = 0;
    bool Breakable = false, CanBeBroken = false, Severed = false;
    double Health = 0, MaxHealth = 0;
    std::vector<std::string> Loot;
    bool Broke() const { return After > Before || Severed; }
};
// A part break is judged against the stock control under the same forced damage.
inline std::string JudgeParts(const std::vector<PartResult>& parts) {
    int creature = 0, control = 0, controlBroke = 0;
    bool breakable = false, broke = false;
    for (const auto& p : parts) {
        if (p.Role == "creature") { ++creature; breakable = breakable || p.Breakable; broke = broke || p.Broke(); }
        else if (p.Role == "control") { ++control; controlBroke += p.Broke(); }
    }
    if (creature == 0 && control > 0) return "the creature has no monster parts (control: " + std::to_string(control) + ")";
    if (controlBroke > 0 && breakable && !broke) return "no creature part broke under forced damage (control: " + std::to_string(controlBroke) + " broke)";
    return {};
}
// Lethal core damage (10x maximum health through a part's ApplyDamageToCore). MetadataKey: the
// actor's GetMetadataTableName, the key its loot and metadata rows use (D33).
// DeathEvents: functions named for death (Death/Died/Dead) that ran on the actor through ProcessEvent.
struct KillResult {
    std::string Role, MetadataKey;
    std::vector<std::string> DeathEvents;
    bool Died = false;
    double HealthBefore = 0, HealthAfter = 0, Seconds = 0;
};
inline std::string JudgeKill(const std::vector<KillResult>& kills) {
    bool creature = false, control = false, creatureTried = false;
    for (const auto& k : kills) {
        if (k.Role == "creature") { creatureTried = true; creature = creature || k.Died; }
        else if (k.Role == "control") control = control || k.Died;
    }
    if (creatureTried && !creature && control) return "the creature did not die from lethal core damage (the control did)";
    return {};
}

// ---- Client: texture streaming --------------------------------------------------------------
inline constexpr int MaxTextures = 1024;
inline constexpr double StreamSettleSeconds = 3, StreamTimeoutSeconds = 90;

// Mips of a full chain (down to 1x1) for the larger side, less the combined LOD bias; at least 1.
inline int FullChainMips(int sizeX, int sizeY, int bias) {
    int size = (std::max)(sizeX, sizeY), mips = 0;
    if (size <= 0) return 0;
    while (size > 0) { ++mips; size >>= 1; }
    return (std::max)(1, mips - (std::max)(0, bias));
}

// One package texture forced fully resident. Initial: resident mips before the force (-1 unknown).
struct TextureState {
    std::string Path;
    int SizeX = 0, SizeY = 0, Bias = 0, Initial = -1, Resident = 0;
    bool Streamable = false, NeverStream = false, Pending = false;
    int Expected() const { return FullChainMips(SizeX, SizeY, Bias); }
    // A streamable texture that settled below its full chain. Reported, not failed: a cooked chain
    // may legitimately stop early, which only live evidence on accepted packages can tell.
    bool Short() const { return Streamable && !NeverStream && !Pending && Resident < Expected(); }
};
struct StreamSummary { int Textures = 0, Streamable = 0, Pending = 0, Short = 0, Raised = 0; };
inline StreamSummary Summarize(const std::vector<TextureState>& textures) {
    StreamSummary s;
    for (const auto& t : textures) {
        ++s.Textures;
        s.Streamable += t.Streamable && !t.NeverStream;
        s.Pending += t.Pending;
        s.Short += t.Short();
        s.Raised += t.Initial >= 0 && t.Resident > t.Initial;
    }
    return s;
}
// Streaming is settled once no update is pending and the resident total held still for
// StreamSettleSeconds. Observe once per poll with the current totals and time.
class StreamSettle {
public:
    bool Observe(long long residentTotal, int pending, double now) {
        if (pending > 0 || !started_ || residentTotal != total_) { total_ = residentTotal; since_ = now; started_ = true; return false; }
        return now - since_ >= StreamSettleSeconds;
    }
private:
    long long total_ = 0;
    double since_ = 0;
    bool started_ = false;
};

// ---- Client: HUD portrait --------------------------------------------------------------------
// The portrait of a live behemoth. Source "hud": the stock HUD health widget bound it and shows Texture.
// Source "asset-row": native code's data path (D33), the actor's identity key naming a row of the game
// instance's BehemothInfo table whose Icon loads as Texture; used when the HUD does not bind (the
// title screen has no hunt). Bound: a texture was found by either path (or the row exists).
struct Portrait {
    bool Bound = false, Revealed = false, Visible = false;
    std::string Texture, Source;  // Texture: object path; empty: no portrait
    int Width = 0, Height = 0;
};
enum class PortraitResult { Shown, Missing, Inconclusive };
inline const char* Name(PortraitResult result) {
    return result == PortraitResult::Shown ? "shown" : result == PortraitResult::Missing ? "missing" : "inconclusive";
}
// An empty creature portrait counts as missing only when the stock control showed one in the same client.
inline PortraitResult Judge(const Portrait& creature, const Portrait* control) {
    if (creature.Bound && !creature.Texture.empty()) return PortraitResult::Shown;
    if (control && control->Bound && !control->Texture.empty()) return PortraitResult::Missing;
    return PortraitResult::Inconclusive;
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
    std::string Role, Class, AnimClass, Key;  // Key: the actor's native identity key (D33)
    bool Spawned = false;
    double Health = 0, MaxHealth = 0, MovedCm = 0;
    std::vector<std::string> Montages;
    MotionTrack Track;
    Portrait Hud;  // client only
    void AddMontage(const std::string& montage) {
        if (montage.empty() || Montages.size() >= static_cast<size_t>(MaxMontages)) return;
        for (const auto& m : Montages) if (m == montage) return;
        Montages.push_back(montage);
    }
};
struct Report {
    std::string Id, Side = "server", Creature, Control, Map, Runtime, Verdict = "running", Reason, CurrentAsset;
    int Port = 0;
    unsigned long Pid = 0;
    int AssetsRequested = 0, AssetsLoaded = 0, AssetsAsClass = 0;  // AsClass: Blueprints loaded as <name>_C
    std::vector<std::string> FailedAssets;
    std::vector<Stage> Stages;
    std::vector<Subject> Subjects;
    std::vector<TextureState> Textures;  // client only
    std::vector<ActionResult> Actions;   // server only
    std::vector<PartResult> Parts;       // server only
    std::vector<KillResult> Kills;       // server only
    long long Frames = 0;                 // client frames rendered while the subjects were drawn
    bool Finished = false;

    Stage& Begin(const std::string& name) { Stages.push_back({name, "running", {}, 0}); return Stages.back(); }
    // The first failing stage decides the verdict; later stages still run when they can.
    void Fail(Stage& stage, const std::string& reason) {
        stage.Status = "failed"; stage.Detail = reason;
        if (Verdict == "running") { Verdict = "fail-" + stage.Name; Reason = reason; }
    }

    std::string ToJson() const {
        const bool client = Side == "client";
        std::string out = "{\n  \"schema\": 1,\n  \"id\": " + Quote(Id) + ",\n  \"side\": " + Quote(Side) + ",\n  \"verdict\": " + Quote(Verdict)
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
                + ", \"spawned\": " + (s.Spawned ? "true" : "false") + ", \"key\": " + Quote(s.Key) + ", \"animClass\": " + Quote(s.AnimClass)
                + ", \"health\": " + Number(s.Health) + ", \"maxHealth\": " + Number(s.MaxHealth) + ", \"movedCm\": " + Number(s.MovedCm)
                + ", \"motion\": {\"verdict\": " + Quote(Name(Judge(s.Track, s.Role == "control" ? nullptr : control)))
                + ", \"samples\": " + std::to_string(s.Track.Samples()) + ", \"bones\": " + std::to_string(s.Track.Bones())
                + ", \"movingBones\": " + std::to_string(s.Track.MovingBones()) + ", \"maxDeltaCm\": " + Number(s.Track.MaxDeltaCm())
                + ", \"invalidSamples\": " + std::to_string(s.Track.Invalid()) + "}, \"montages\": [";
            for (size_t m = 0; m < s.Montages.size(); ++m) out += std::string(m ? ", " : "") + Quote(s.Montages[m]);
            out += "]";
            if (client) {
                const Portrait* controlHud = nullptr;
                for (const auto& other : Subjects) if (other.Role == "control") controlHud = &other.Hud;
                const auto& p = s.Hud;
                out += ", \"portrait\": {\"verdict\": " + Quote(Name(Judge(p, s.Role == "control" ? nullptr : controlHud)))
                    + ", \"bound\": " + (p.Bound ? "true" : "false") + ", \"revealed\": " + (p.Revealed ? "true" : "false")
                    + ", \"visible\": " + (p.Visible ? "true" : "false") + ", \"source\": " + Quote(p.Source) + ", \"texture\": " + Quote(p.Texture)
                    + ", \"width\": " + std::to_string(p.Width) + ", \"height\": " + std::to_string(p.Height) + "}";
            }
            out += "}";
        }
        out += std::string(Subjects.empty() ? "" : "\n  ") + "]";
        if (!Actions.empty()) {
            out += ",\n  \"actions\": [";
            for (size_t i = 0; i < Actions.size(); ++i) {
                const auto& a = Actions[i];
                out += std::string(i ? "," : "") + "\n    {\"row\": " + Quote(a.Row) + ", \"action\": " + Quote(a.Action) + ", \"montage\": " + Quote(a.Montage)
                    + ", \"played\": " + (a.Played ? "true" : "false") + ", \"length\": " + Number(a.Length) + ", \"seconds\": " + Number(a.Seconds)
                    + ", \"notifies\": " + std::to_string(a.Notifies) + ", \"spawned\": " + std::to_string(a.Spawned)
                    + ", \"role\": " + Quote(a.Role) + ", \"route\": " + Quote(a.Route)
                    + ", \"selectedMontage\": " + Quote(a.SelectedMontage) + ", \"error\": " + Quote(a.Error)
                    + ", \"observed\": " + (a.Observed ? "true" : "false") + ", \"completed\": " + (a.Completed ? "true" : "false") + "}";
            }
            out += "\n  ]";
        }
        if (!Parts.empty()) {
            out += ",\n  \"parts\": [";
            for (size_t i = 0; i < Parts.size(); ++i) {
                const auto& p = Parts[i];
                out += std::string(i ? "," : "") + "\n    {\"role\": " + Quote(p.Role) + ", \"name\": " + Quote(p.Name) + ", \"stages\": " + std::to_string(p.Stages)
                    + ", \"breakable\": " + (p.Breakable ? "true" : "false") + ", \"canBeBroken\": " + (p.CanBeBroken ? "true" : "false") + ", \"before\": " + std::to_string(p.Before) + ", \"after\": " + std::to_string(p.After)
                    + ", \"severed\": " + (p.Severed ? "true" : "false") + ", \"broke\": " + (p.Broke() ? "true" : "false")
                    + ", \"health\": " + Number(p.Health) + ", \"maxHealth\": " + Number(p.MaxHealth) + ", \"loot\": [";
                for (size_t l = 0; l < p.Loot.size(); ++l) out += std::string(l ? ", " : "") + Quote(p.Loot[l]);
                out += "]}";
            }
            out += "\n  ]";
        }
        if (!Kills.empty()) {
            out += ",\n  \"kills\": [";
            for (size_t i = 0; i < Kills.size(); ++i) {
                const auto& k = Kills[i];
                out += std::string(i ? "," : "") + "\n    {\"role\": " + Quote(k.Role) + ", \"metadataKey\": " + Quote(k.MetadataKey)
                    + ", \"died\": " + (k.Died ? "true" : "false") + ", \"healthBefore\": " + Number(k.HealthBefore) + ", \"healthAfter\": " + Number(k.HealthAfter)
                    + ", \"seconds\": " + Number(k.Seconds) + ", \"deathEvents\": [";
                for (size_t e = 0; e < k.DeathEvents.size(); ++e) out += std::string(e ? ", " : "") + Quote(k.DeathEvents[e]);
                out += "]}";
            }
            out += "\n  ]";
        }
        if (client) {
            const auto s = Summarize(Textures);
            out += ",\n  \"frames\": " + std::to_string(Frames) + ",\n  \"stream\": {\"textures\": " + std::to_string(s.Textures)
                + ", \"streamable\": " + std::to_string(s.Streamable) + ", \"raised\": " + std::to_string(s.Raised)
                + ", \"pending\": " + std::to_string(s.Pending) + ", \"short\": " + std::to_string(s.Short) + ", \"list\": [";
            for (size_t i = 0; i < Textures.size(); ++i) {
                const auto& t = Textures[i];
                out += std::string(i ? "," : "") + "\n    {\"path\": " + Quote(t.Path) + ", \"size\": [" + std::to_string(t.SizeX) + ", " + std::to_string(t.SizeY)
                    + "], \"bias\": " + std::to_string(t.Bias) + ", \"initial\": " + std::to_string(t.Initial) + ", \"resident\": " + std::to_string(t.Resident)
                    + ", \"expected\": " + std::to_string(t.Expected()) + ", \"streamable\": " + (t.Streamable && !t.NeverStream ? "true" : "false")
                    + ", \"pending\": " + (t.Pending ? "true" : "false") + "}";
            }
            out += std::string(Textures.empty() ? "" : "\n  ") + "]}";
        }
        return out + "\n}\n";
    }
};
}
