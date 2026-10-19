#include "../DevelopmentAccessPolicy.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace DevelopmentAccessPolicy;
    assert(!Enabled);
    const std::wstring dojo = L"/Game/Maps/islands/dojo/training_dojo_persistent?game=/Game/Blueprints/GameMode/BPGM_ArchonTrainingGrounds.BPGM_ArchonTrainingGrounds_C?MaxPlayers=4";
    assert(StockMapURL(dojo,L"NO_BEHEMOTH",L"CR19_MatchmakerHunt_ShatteredIsles_TrainingDojo",L"NO_EXPECTED_PLAYERS")
        == dojo + L"?HuntId=CR19_MatchmakerHunt_ShatteredIsles_TrainingDojo");
    assert(StockMapURL(L"/Game/Maps/ramsgate/ramsgate_01_persistent",L"NO_BEHEMOTH",L"NO_MM_HUNTID",L"NO_EXPECTED_PLAYERS")
        == L"/Game/Maps/ramsgate/ramsgate_01_persistent");
    assert(StockMapURL(L"/Game/Maps/islands/1803/frida_moss_falls",L"/Game/Monsters/drask144/fulgar_alpha_bp.fulgar_alpha_bp_C",
        L"Thunderdeep144_MatchmakerHunt_Drask",L"test-player:Thunderdeep144_PlayerHunt_Pursuit_Drask")
        == L"/Game/Maps/islands/1803/frida_moss_falls?MonsterClass=/Game/Monsters/drask144/fulgar_alpha_bp.fulgar_alpha_bp_C?HuntId=Thunderdeep144_MatchmakerHunt_Drask?PlayerHuntIds=test-player:Thunderdeep144_PlayerHunt_Pursuit_Drask");
    std::cout << "PASS stock Training Grounds map/mode/hunt, city, reserved monster/roster, development access disabled\n";
}
