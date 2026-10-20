// Content registry parsing, lookups and conflicts (ContentRegistry.h). AGPL-3.0-only.
#include "registry-fixture.h"
#include <cassert>
#include <iostream>

using namespace ContentRegistry;

static bool Has(const std::vector<std::string>& errors, const std::string& part) {
    for (const auto& e : errors) if (e.find(part) != std::string::npos) return true;
    return false;
}

int main() {
    const auto registry = BackportRegistry();
    assert(registry.Errors.empty() && registry.Entries.size() == 3);

    // Strength restore: registered hunt and exactly its creature.
    const auto frost = registry.AuthoredFor("Frost144_MatchmakerHunt_Gnasher", "/Game/Monsters/frost144/mcbeaver_frost_bp.mcbeaver_frost_bp_C");
    assert(frost && frost->Power == 240 && frost->Resistance == 120);
    assert(!registry.AuthoredFor("Frost144_MatchmakerHunt_Gnasher", "/Game/Monsters/mcrollin/mcbeaver_normal_bp.mcbeaver_normal_bp_C"));
    assert(!registry.AuthoredFor("CR19_MatchmakerHunt_Beaver", "/Game/Monsters/frost144/mcbeaver_frost_bp.mcbeaver_frost_bp_C"));
    assert(!registry.AuthoredFor("TestArena144_MatchmakerHunt", ""));

    // Menu: matchmaker hunt first, then player hunt; destinations have no strength.
    assert(registry.PresentedFor("", "Thunderdeep144_MatchmakerHunt_Drask")->Power == 420);
    assert(registry.PresentedFor("Thunderdeep144_PlayerHunt_Pursuit_Drask", "")->Resistance == 330);
    assert(!registry.PresentedFor("Frost144_PlayerHunt_Pursuit_Gnasher", "CR19_MatchmakerHunt_Beaver"));
    assert(!registry.PresentedFor("TestArena144_PlayerHunt", ""));
    assert(Recommended(*frost).Weapon == 120 && Recommended(*frost).Armour == 240);

    // Return to Ramsgate: world package and hunt must match; a destination also needs its game mode.
    assert(WorldPackage("/Game/Maps/islands/1609/bella_jen_snow.bella_jen_snow") == "/Game/Maps/islands/1609/bella_jen_snow");
    assert(registry.OffersReturn("/Game/Maps/islands/1609/bella_jen_snow", "Frost144_MatchmakerHunt_Gnasher"));
    assert(!registry.OffersReturn("/Game/Maps/islands/1609/bella_jen_snow", "CR19_MatchmakerHunt_Beaver"));
    assert(!registry.OffersReturn("/Game/Maps/islands/1705/dia_moss_triforce", "Frost144_MatchmakerHunt_Gnasher"));
    const auto mode = "/Game/Blueprints/BPGM_Archon_Prototype.BPGM_Archon_Prototype_C";
    assert(registry.OffersReturn("/Game/Maps/islands/arenas/arena_ramsgate_00", "TestArena144_MatchmakerHunt", mode));
    assert(!registry.OffersReturn("/Game/Maps/islands/arenas/arena_ramsgate_00", "TestArena144_MatchmakerHunt", "/Game/Blueprints/Other.Other_C"));
    assert(!registry.OffersReturn("/Game/Maps/islands/arenas/arena_ramsgate_00", "", mode));

    // An empty or missing registry registers nothing (core-only profiles).
    assert(Parse("").Empty() && Parse("").Errors.empty());
    assert(Parse("vibeless-registry 1\n").Empty());
    assert(Has(Parse("hello\n").Errors, "not a vibeless-registry 1 document"));

    // Invalid lines are reported and skipped; the rest still loads.
    const auto mixed = Parse(std::string(BackportRegistryText())
        + "hunt p H1 player=P1 world=/W creature=/C power=0 resistance=5\n"
        + "hunt p H2 player=P2 world=../W creature=/C power=5 resistance=5\n"
        + "destination p H3 player=P3 world=/W mode=/M power=5\n"
        + "hunt p H4 player=P4 world=/W creature=/C power=5 resistance=5 colour=red\n"
        + "hunt p H5 player=P5 world=/W creature=/C power=5 resistance=5 return=yes\n"
        + "spawner p H6\n"
        + "hunt p H7 player=P7 player=P8 world=/W creature=/C power=5 resistance=5\n");
    assert(mixed.Entries.size() == 3 && mixed.Errors.size() == 7);
    assert(Has(mixed.Errors, "line 5: a hunt needs power= and resistance= from 1 to 2000"));
    assert(Has(mixed.Errors, "line 6: needs player= and world="));
    assert(Has(mixed.Errors, "a destination needs mode= and no creature or strength"));
    assert(Has(mixed.Errors, "unknown field colour"));
    assert(Has(mixed.Errors, "return must be 0 or 1"));
    assert(Has(mixed.Errors, "unknown entry spawner"));
    assert(Has(mixed.Errors, "each once"));

    // Two packages claiming one hunt: the second is refused and both are named.
    const auto clash = Parse(std::string(BackportRegistryText())
        + "hunt example.copy Frost144_MatchmakerHunt_Gnasher player=Other world=/W creature=/C power=5 resistance=5\n");
    assert(clash.Entries.size() == 3);
    assert(Has(clash.Errors, "conflict: example.copy registers Frost144_MatchmakerHunt_Gnasher where backport.deepfrost-gnasher already registered"));

    std::cout << "content registry tests passed" << std::endl;
    return 0;
}
