#include "../TestArenaPolicy.h"
#include "../ServerRpcGuard.h"
#include <cassert>
#include <string>
int main() {
    using namespace TestArenaPolicy;
    assert(IsArena(World,MatchmakerHunt,GameMode));
    assert(!IsArena(World,"Arena_MatchmakerHunt_Hard_001",GameMode));
    assert(!IsArena("/Game/Maps/islands/dojo/training_dojo_persistent.training_dojo_persistent",MatchmakerHunt,GameMode));
    assert(!IsArena(World,MatchmakerHunt,"BPGM_ArchonTrainingGrounds_C"));
    auto spawn=Parse("@arena144|spawn|beaver_frost");
    assert(spawn && spawn->Operation==Action::Spawn && spawn->Id=="beaver_frost");
    auto despawn=Parse("@arena144|despawn|fulgar_alpha");
    assert(despawn && despawn->Operation==Action::Despawn);
    assert(Parse("@arena144|clear")->Operation==Action::Clear);
    for(auto text:{"spawn|beaver", "@arena144|spawn|", "@arena144|spawn|/Game/Monsters/x", "@arena144|clear|extra",
        "@arena144|spawn|beaver|clear", "@arena144|exec|god", "@arena144|spawn|beaver\nclear"}) assert(!Parse(text));
    assert(!Parse("@arena144|spawn|"+std::string(200,'a')));
    // The narrow arena transport does not open normal cheat RPCs elsewhere.
    assert(IsBlockedServerRpc("Function Archon.ArchonPlayerController.ServerCheat"));
    assert(MaxLiving==3);
    assert(DisplayName("beaver_heroic+","Ragetail Gnasher (Heroic)")=="Ragetail Gnasher (Heroic+)");
    assert(DisplayName("beaver_tutorial","Lesser Gnasher")=="Lesser Gnasher (Tutorial)");
    assert(DisplayName("crowbear_clone","Shrowd")=="Shrowd (Clone)");
    assert(DisplayName("fulgar_alpha","Thunderdeep Drask")=="Thunderdeep Drask");
}
