#include "../HuntPresentationPolicy.h"
#include <cassert>
int main() {
    using namespace HuntPresentationPolicy;
    auto frost = ForHunt("Frost144_PlayerHunt_Pursuit_Gnasher", "");
    assert(frost && frost->Power == 240 && frost->Resistance == 120);
    auto drask = ForHunt("", "Thunderdeep144_MatchmakerHunt_Drask");
    assert(drask && drask->Power == 420 && drask->Resistance == 330);
    assert(Recommended(*frost).Weapon == 120 && Recommended(*frost).Armour == 240);
    assert(Recommended(*drask).Weapon == 330 && Recommended(*drask).Armour == 420);
    assert(!ForHunt("Frost144_PlayerHunt_Pursuit_Gnasher", "CR19_MatchmakerHunt_Beaver"));
    assert(!ForHunt("CR19_PlayerHunt_Pursuit_Beaver", ""));
    assert(!ForHunt("", ""));
}
