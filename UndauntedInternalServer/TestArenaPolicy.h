#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace TestArenaPolicy {
inline constexpr char PlayerHunt[] = "TestArena144_PlayerHunt";
inline constexpr char MatchmakerHunt[] = "TestArena144_MatchmakerHunt";
inline constexpr char Map[] = "/Game/Maps/islands/arenas/arena_ramsgate_00";
inline constexpr char World[] = "/Game/Maps/islands/arenas/arena_ramsgate_00.arena_ramsgate_00";
inline constexpr char GameMode[] = "/Game/Blueprints/BPGM_Archon_Prototype.BPGM_Archon_Prototype_C";
inline constexpr std::string_view Prefix = "@arena144|";
inline constexpr int MaxLiving = 3;
inline std::string DisplayName(std::string_view id,std::string label) {
    if(id.ends_with("+")) {
        const auto heroic=label.find("(Heroic)");
        if(heroic!=std::string::npos) label.replace(heroic,8,"(Heroic+)");
        else label+=" (Heroic+)";
    }
    if(id.ends_with("_tutorial")) label+=" (Tutorial)";
    if(id.ends_with("_clone")) label+=" (Clone)";
    if(id.find("PAXdemo")!=std::string_view::npos) label+=" (Demo)";
    if(label.starts_with("~DNT~ ")) {label.erase(0,6);label+=" (Prototype)";}
    return label;
}
inline bool IsArena(std::string_view world, std::string_view hunt, std::string_view mode) {
    return world == World && hunt == MatchmakerHunt && mode == GameMode;
}
enum class Action { Spawn, Despawn, Clear };
struct Command { Action Operation; std::string Id; };
inline std::optional<Command> Parse(std::string_view text) {
    if (!text.starts_with(Prefix) || text.size() > 180) return {};
    text.remove_prefix(Prefix.size());
    if (text == "clear") return Command{Action::Clear,{}};
    const auto separator = text.find('|');
    if (separator == std::string_view::npos) return {};
    const auto verb = text.substr(0,separator), id = text.substr(separator+1);
    if ((verb != "spawn" && verb != "despawn") || id.empty() || id.size() > 128) return {};
    for (const auto c : id) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '+')) return {};
    return Command{verb == "spawn" ? Action::Spawn : Action::Despawn,std::string(id)};
}
}
