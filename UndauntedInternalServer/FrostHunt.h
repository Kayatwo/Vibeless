// Validate the cooked dedicated pursuit through the stock 1.4.4 catalog. AGPL-3.0-only.
#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include "SDK.hpp"
#include "SDK/Archon_parameters.hpp"
#include "FrostHuntConfig.h"

namespace FrostHunt {
using namespace SDK;
inline ULONGLONG NextPoll = 0;
inline UWorld* LastWorld = nullptr;
inline std::string LastSnapshot;
inline void Log(const std::string& body) {
    std::ofstream out(std::filesystem::path(DojoProbe::Root()) / L"reports/frost-hunt.jsonl",std::ios::app);
    out << "{\"pid\":" << GetCurrentProcessId() << ",\"uptimeMs\":" << GetTickCount64() << ',' << body << "}\n";
}
inline bool HasTag(const FGameplayTagContainer& tags,const char* name) {
    if(tags.GameplayTags.Num()<0 || tags.GameplayTags.Num()>32) return false;
    for(const auto& tag:tags.GameplayTags) if(tag.TagName.GetRawString()==name) return true;
    return false;
}
// Read-only: no table mutation, unlock bypass, replacement UI or catalog hook.
// The mounted old-format tables feed the ordinary native catalog before setup.
inline void Tick(UWorld* world) {
    if(!DojoProbe::IsExpectedProcess() || !world || GetTickCount64()<NextPoll) return;
    NextPoll=GetTickCount64()+2000;
    auto* instance=UGameplayStatics::GetGameInstance(world);
    auto* type=UObject::FindClassFast("ArchonGameInstance");
    if(!instance || !type || !instance->IsA(type)) return;
    auto* game=static_cast<UArchonGameInstance*>(instance);
    auto* catalog=game->HuntCatalog;
    if(!catalog || !catalog->IsAvailable()) return;
    FPlayerHuntTableData player{};
    FMatchmakerHuntTableData matchmaker{};
    const auto playerId=FString(L"Frost144_PlayerHunt_Pursuit_Gnasher");
    const auto matchmakerId=FString(L"Frost144_MatchmakerHunt_Gnasher");
    const bool hasPlayer=catalog->GetPlayerHunt(playerId,&player);
    const bool hasMatchmaker=catalog->GetMatchmakerHunt(matchmakerId,&matchmaker);
    auto* controller=UArchonGameplayStatics::GetArchonLocalPlayerController(world);
    bool playerUnlocked=false,matchmakerUnlocked=false;
    int options=-1,unlockedOptions=-1,unlockedBehemoths=-1;
    if(controller && hasPlayer && hasMatchmaker) {
        playerUnlocked=catalog->IsHuntUnlocked(playerId,player.Unlock,player.AltUnlock,controller);
        matchmakerUnlocked=catalog->IsHuntUnlocked(matchmakerId,matchmaker.Unlock,matchmaker.AltUnlock,controller);
        options=catalog->GetHuntMatchmakingOptions(playerId,controller).Num();
        unlockedOptions=catalog->GetUnlockedMatchmakingOptions(playerId,controller).Num();
        unlockedBehemoths=catalog->GetUnlockedMatchmakingBehemoths(playerId,controller).Num();
    }
    FBehemothInfo* asset=nullptr;
    auto* table=game->BehemothData;
    if(table && table->RowStruct && table->RowStruct->GetName()=="BehemothInfo"
        && table->RowMap.NumAllocated()>=0 && table->RowMap.NumAllocated()<=512) {
        for(int32 index=0;index<table->RowMap.NumAllocated();++index)
            if(table->RowMap.IsValidIndex(index) && table->RowMap[index].Key().ToString()=="beaver_frost")
                asset=reinterpret_cast<FBehemothInfo*>(table->RowMap[index].Value());
    }
    const auto text=[](const FText& value){return value.TextData?value.ToString():std::string{};};
    bool hasElement=false; FBehemothMetaDataInfo metadata{};
    if(hasPlayer && hasMatchmaker && asset) if(auto* ui=UArchonGameplayStatics::GetArchonGlobalUI(world))
        if(auto* function=DojoProbe::FindFunction(ui,"GetBehemothMetaDataBP")) {
            Params::ArchonGlobalUI_GetBehemothMetaDataBP query{};
            query.BehemothName=UKismetStringLibrary::Conv_StringToName(FString(L"beaver_frost"));
            ui->ProcessEvent(function,&query);
            hasElement=query.ReturnValue; metadata=query.BehemothMetaDataInfo;
        }
    const auto snapshot="\"playerRow\":"+std::string(DojoProbe::Bool(hasPlayer))+",\"matchmakerRow\":"+DojoProbe::Bool(hasMatchmaker)
        +",\"title\":"+DojoProbe::Json(hasPlayer?text(player.HuntName):std::string{})
        +",\"frostTag\":"+DojoProbe::Bool(hasPlayer&&HasTag(player.HuntTags,"Hunt.Ice"))
        +",\"frostMenuTag\":\"Hunt.Ice\""
        +",\"neutralTag\":"+DojoProbe::Bool(hasPlayer&&HasTag(player.HuntTags,"Hunt.Neutral"))
        +",\"huntReferences\":"+std::to_string(hasPlayer?player.MatchmakerHuntIDs.Num():0)
        +",\"localController\":"+DojoProbe::Bool(controller!=nullptr)
        +",\"playerUnlocked\":"+DojoProbe::Bool(playerUnlocked)+",\"matchmakerUnlocked\":"+DojoProbe::Bool(matchmakerUnlocked)
        +",\"matchmakingOptions\":"+std::to_string(options)+",\"unlockedOptions\":"+std::to_string(unlockedOptions)
        +",\"unlockedBehemoths\":"+std::to_string(unlockedBehemoths)
        +",\"behemoth\":"+DojoProbe::Json(hasMatchmaker?matchmaker.SpecificBehemoth.BehemothName.ToString():std::string{})
        +",\"monsterPath\":"+DojoProbe::Json(hasMatchmaker?matchmaker.SpecificBehemoth.BehemothAsset.ObjectID.AssetPathName.GetRawString():std::string{})
        +",\"mapCount\":"+std::to_string(hasMatchmaker?matchmaker.MapList.Num():0)
        +",\"assetMetadataRow\":"+DojoProbe::Bool(asset!=nullptr)
        +",\"assetTitle\":"+DojoProbe::Json(asset?text(asset->DisplayName):std::string{})
        +",\"portrait\":"+DojoProbe::Json(asset?asset->Icon.ObjectID.AssetPathName.GetRawString():std::string{})
        +",\"elementAvailable\":"+DojoProbe::Bool(hasElement)
        +",\"element\":"+std::to_string(hasElement?static_cast<int>(metadata.ElementType):-1);
    if(LastWorld==world && LastSnapshot==snapshot) return;
    LastWorld=world; LastSnapshot=snapshot;
    Log("\"event\":\"catalog_validation\",\"map\":"+DojoProbe::Json(world->GetName())+','+snapshot);
}
}
