// Vibeless content registry (Phase 2): what packages tell the shared creature/hunt services about
// their content, instead of those services hard-coding each package's IDs. AGPL-3.0-only.
//
// The package composer writes it from the packages' "native" manifest sections, into
// <tables>/native/registry.txt (the profile's composed tables). One line per registration:
//
//   vibeless-registry 1
//   hunt <package> <matchmakerHunt> player=<playerHunt> world=<map> creature=<class> power=<n> resistance=<n> return=1
//   destination <package> <matchmakerHunt> player=<playerHunt> world=<map> mode=<gameModeClass> return=1
//
//   world     the map package path, as the deploy server starts it (/Game/Maps/islands/1609/bella_jen_snow)
//   creature  the behemoth class whose authored strength is restored (/Game/Monsters/.../x_bp.x_bp_C)
//   power, resistance  the creature's authored GlobalPower / GlobalResistance (1 to 2000)
//   return=1  the hunt or destination offers the native "Return to Ramsgate" item
//
// Shared services that read it: authored strength restoration (server), menu recommendations
// (client: weapon target = resistance, armour target = power) and return-to-Ramsgate scope (client).
// Equipment, recipes and loot come from the packages' cooked tables; no shared service needs them
// registered yet. Portable: no Windows or SDK types.
#pragma once
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ContentRegistry {
struct Strength { float Power = 0, Resistance = 0; };

struct Entry {
    enum class Kind { Hunt, Destination } Type = Kind::Hunt;
    std::string Package, MatchmakerHunt, PlayerHunt, World, Creature, Mode;
    std::optional<Strength> Authored;
    bool Return = false;
};

struct Registry {
    std::vector<Entry> Entries;
    std::vector<std::string> Errors;

    bool Empty() const { return Entries.empty(); }
    const Entry* ByMatchmaker(std::string_view hunt) const {
        for (const auto& e : Entries) if (!hunt.empty() && e.MatchmakerHunt == hunt) return &e;
        return nullptr;
    }
    const Entry* ByPlayer(std::string_view hunt) const {
        for (const auto& e : Entries) if (!hunt.empty() && e.PlayerHunt == hunt) return &e;
        return nullptr;
    }
    // The authored strength to restore for a spawned creature of this hunt, or nothing.
    std::optional<Strength> AuthoredFor(std::string_view matchmakerHunt, std::string_view creatureClass) const {
        const auto* e = ByMatchmaker(matchmakerHunt);
        if (!e || e->Type != Entry::Kind::Hunt || e->Creature != creatureClass) return {};
        return e->Authored;
    }
    // Strength shown in the hunt menu: an explicit matchmaker hunt takes precedence over the player hunt.
    std::optional<Strength> PresentedFor(std::string_view playerHunt, std::string_view matchmakerHunt) const {
        const auto* e = !matchmakerHunt.empty() ? ByMatchmaker(matchmakerHunt) : ByPlayer(playerHunt);
        if (!e || e->Type != Entry::Kind::Hunt) return {};
        return e->Authored;
    }
    // Is this world a registered hunt or destination of the given matchmaker hunt? worldPackage is
    // the map's package path; mode is the game mode class path (destinations require it to match).
    const Entry* WorldEntry(std::string_view worldPackage, std::string_view matchmakerHunt, std::string_view mode = {}) const {
        const auto* e = ByMatchmaker(matchmakerHunt);
        if (!e || e->World != worldPackage) return nullptr;
        if (e->Type == Entry::Kind::Destination && e->Mode != mode) return nullptr;
        return e;
    }
    bool OffersReturn(std::string_view worldPackage, std::string_view matchmakerHunt, std::string_view mode = {}) const {
        const auto* e = WorldEntry(worldPackage, matchmakerHunt, mode);
        return e && e->Return;
    }
};

// Menu targets: player weapons oppose the creature's resistance, armour opposes its power.
struct Targets { float Weapon, Armour; };
inline Targets Recommended(const Strength& creature) { return {creature.Resistance, creature.Power}; }

inline bool ValidToken(std::string_view text, bool path) {
    if (text.empty() || text.size() > 200) return false;
    for (const unsigned char c : text) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '_' || c == '-' || c == '.' || (path && c == '/');
        if (!ok) return false;
    }
    return text.find("..") == std::string_view::npos && (!path || text.front() == '/');
}

inline std::optional<float> ParseStat(const std::string& text) {
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end || !std::isfinite(value) || value < 1 || value > 2000) return {};
    return value;
}

// Parses a registry document. Invalid lines are reported in Errors and skipped; a second entry
// for a matchmaker hunt or player hunt already registered is a conflict naming both packages.
inline Registry Parse(const std::string& text) {
    Registry out;
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line)) return out;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "vibeless-registry 1") { out.Errors.push_back("not a vibeless-registry 1 document"); return out; }
    int number = 1;
    while (std::getline(input, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto bad = [&](const std::string& why) { out.Errors.push_back("registry line " + std::to_string(number) + ": " + why); };
        std::istringstream fields(line);
        std::string kind, package, hunt, token;
        fields >> kind >> package >> hunt;
        if (kind != "hunt" && kind != "destination") { bad("unknown entry " + kind); continue; }
        if (!ValidToken(package, false) || !ValidToken(hunt, false)) { bad("needs a package ID and a matchmaker hunt ID"); continue; }
        Entry e;
        e.Type = kind == "hunt" ? Entry::Kind::Hunt : Entry::Kind::Destination;
        e.Package = package;
        e.MatchmakerHunt = hunt;
        std::map<std::string, std::string> values;
        bool malformed = false;
        while (fields >> token) {
            const auto eq = token.find('=');
            if (eq == std::string::npos || eq == 0 || !values.emplace(token.substr(0, eq), token.substr(eq + 1)).second) { malformed = true; break; }
        }
        if (malformed) { bad("fields must be key=value, each once"); continue; }
        const auto take = [&](const char* key) { auto it = values.find(key); std::string v = it == values.end() ? "" : it->second; if (it != values.end()) values.erase(it); return v; };
        e.PlayerHunt = take("player");
        e.World = take("world");
        e.Creature = take("creature");
        e.Mode = take("mode");
        const auto power = take("power"), resistance = take("resistance"), ret = take("return");
        if (!values.empty()) { bad("unknown field " + values.begin()->first); continue; }
        if (!ValidToken(e.PlayerHunt, false) || !ValidToken(e.World, true)) { bad("needs player= and world="); continue; }
        if (!ret.empty() && ret != "0" && ret != "1") { bad("return must be 0 or 1"); continue; }
        e.Return = ret == "1";
        if (e.Type == Entry::Kind::Hunt) {
            if (!ValidToken(e.Creature, true) || !e.Mode.empty()) { bad("a hunt needs creature= and no mode="); continue; }
            const auto p = ParseStat(power), r = ParseStat(resistance);
            if (!p || !r) { bad("a hunt needs power= and resistance= from 1 to 2000"); continue; }
            e.Authored = Strength{*p, *r};
        } else {
            if (!ValidToken(e.Mode, true) || !e.Creature.empty() || !power.empty() || !resistance.empty()) {
                bad("a destination needs mode= and no creature or strength"); continue;
            }
        }
        const Entry* clash = nullptr;
        for (const auto& other : out.Entries)
            if (other.MatchmakerHunt == e.MatchmakerHunt || other.PlayerHunt == e.PlayerHunt) { clash = &other; break; }
        if (clash) { bad("conflict: " + e.Package + " registers " + e.MatchmakerHunt + " where " + clash->Package + " already registered " + clash->MatchmakerHunt); continue; }
        out.Entries.push_back(std::move(e));
    }
    return out;
}

// The map package of a world object path ("/Game/Maps/x/y.y" -> "/Game/Maps/x/y").
inline std::string WorldPackage(std::string_view objectPath) {
    const auto dot = objectPath.find('.');
    return std::string(dot == std::string_view::npos ? objectPath : objectPath.substr(0, dot));
}
}
