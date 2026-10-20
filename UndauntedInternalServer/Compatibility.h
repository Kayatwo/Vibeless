// Vibeless multiplayer compatibility contract (Phase 2), portable part. AGPL-3.0-only.
//
// A server that runs packages publishes what a joining client needs at GET /vibeless/requirements
// (the metagame serves it from the profile's composition):
//
//   vibeless-requirements 1
//   foundation 0.1.0
//   feature deepfrost
//   package backport.deepfrost-gnasher 1.0.0
//   file backport.deepfrost-gnasher Frost144_Hunt_P.pak <sha256>
//
// The client compares that with what it has and runs, and reports its state to POST
// /vibeless/compatibility in the same line format:
//
//   vibeless-client 1
//   foundation 0.1.0
//   feature deepfrost
//   file Frost144_Hunt_P.pak <sha256 or "missing">
//
// Rules (shared with the metagame's check): the foundation's major.minor must match; the client must
// run exactly the server's native features (an extra feature adds content the server cannot serve);
// every required file must be present with its hash. A server without requirements accepts any client
// that runs no features. Problems are plain sentences naming the package, for the player.
#pragma once
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace Compatibility {
struct RequiredFile { std::string Package, Name, Sha256; };
struct Requirements {
    std::string Foundation;                       // empty: the server does not say
    std::set<std::string> Features;
    std::map<std::string, std::string> Packages;  // id -> version
    std::vector<RequiredFile> Files;
    bool Empty() const { return Features.empty() && Packages.empty() && Files.empty(); }
};
struct ClientState {
    std::string Foundation;
    std::set<std::string> Features;
    std::map<std::string, std::string> Files;     // name -> sha256, "" when missing
};

inline bool ValidId(std::string_view text) {
    return !text.empty() && text.size() <= 96 && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '-' || c == '_';
    });
}
// Only plain .pak names: the client looks them up in its own Paks folder and nowhere else.
inline bool ValidPakName(std::string_view name) {
    return name.size() > 4 && name.size() <= 128 && name.substr(name.size() - 4) == ".pak"
        && name.find("..") == std::string_view::npos && ValidId(name);
}
inline bool ValidSha256(std::string_view hash) {
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
    });
}
inline bool ValidVersion(std::string_view text) {
    int dots = 0;
    for (const char c : text) {
        if (c == '.') ++dots;
        else if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return dots == 2 && text.front() != '.' && text.back() != '.' && text.find("..") == std::string_view::npos;
}
inline std::string MajorMinor(const std::string& version) {
    const auto last = version.rfind('.');
    return last == std::string::npos ? version : version.substr(0, last);
}

// Returns an error, or empty when `text` is a valid requirements document.
inline std::string ParseRequirements(const std::string& text, Requirements& out) {
    out = {};
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line) || (line != "vibeless-requirements 1" && line != "vibeless-requirements 1\r"))
        return "not a vibeless-requirements 1 document";
    int number = 1;
    while (std::getline(input, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::istringstream fields(line);
        std::string kind, a, b, c, extra;
        fields >> kind >> a >> b >> c >> extra;
        const auto bad = [&](const char* why) { return "line " + std::to_string(number) + ": " + why; };
        if (!extra.empty()) return bad("too many fields");
        if (kind == "foundation") {
            if (!ValidVersion(a) || !b.empty()) return bad("foundation needs one version");
            out.Foundation = a;
        } else if (kind == "feature") {
            if (!ValidId(a) || !b.empty()) return bad("feature needs one ID");
            out.Features.insert(a);
        } else if (kind == "package") {
            if (!ValidId(a) || !ValidVersion(b) || !c.empty()) return bad("package needs an ID and a version");
            if (!out.Packages.emplace(a, b).second) return bad("package listed twice");
        } else if (kind == "file") {
            if (!out.Packages.count(a)) return bad("file names a package that is not listed before it");
            if (!ValidPakName(b) || !ValidSha256(c)) return bad("file needs a .pak name and a SHA-256");
            for (const auto& file : out.Files)
                if (file.Name == b) return bad("file listed twice");
            out.Files.push_back({a, b, c});
        } else {
            return bad("unknown entry");
        }
    }
    return {};
}

inline std::vector<std::string> Check(const Requirements& required, const ClientState& client) {
    std::vector<std::string> problems;
    if (required.Empty() && client.Features.empty()) return problems;
    if (!required.Foundation.empty() && MajorMinor(required.Foundation) != MajorMinor(client.Foundation))
        problems.push_back("The server runs Vibeless " + required.Foundation + "; this game has Vibeless "
            + (client.Foundation.empty() ? std::string("(unknown)") : client.Foundation) + ".");
    for (const auto& feature : required.Features)
        if (!client.Features.count(feature))
            problems.push_back("The server uses the native feature \"" + feature + "\", which this game does not run.");
    for (const auto& feature : client.Features)
        if (!required.Features.count(feature))
            problems.push_back("This game runs the native feature \"" + feature + "\", which the server does not use.");
    for (const auto& file : required.Files) {
        const auto found = client.Files.find(file.Name);
        const auto label = file.Package + " " + required.Packages.at(file.Package);
        if (found == client.Files.end() || found->second.empty())
            problems.push_back(label + ": " + file.Name + " is missing.");
        else if (found->second != file.Sha256)
            problems.push_back(label + ": " + file.Name + " is a different version.");
    }
    return problems;
}

inline std::string Report(const ClientState& client) {
    std::string out = "vibeless-client 1\nfoundation " + client.Foundation + "\n";
    for (const auto& feature : client.Features) out += "feature " + feature + "\n";
    for (const auto& [name, hash] : client.Files) out += "file " + name + " " + (hash.empty() ? "missing" : hash) + "\n";
    return out;
}
}
