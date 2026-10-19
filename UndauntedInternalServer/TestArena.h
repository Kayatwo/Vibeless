#pragma once
#include "TestArenaPolicy.h"
#include "FrostEncounter.h"
#include "SDK/Engine_parameters.hpp"
#include <algorithm>
#include <map>

namespace TestArena {
using namespace SDK;
struct Creature { std::string Id, Label, Path; };
inline bool IsWorld(UWorld* world) {
    if (!DojoProbe::IsExpectedProcess() || !world || !world->GameState
        || !world->GameState->IsA(AArchonGameState::StaticClass())) return false;
    auto* state = static_cast<AArchonGameState*>(world->GameState);
    return TestArenaPolicy::IsArena(DojoProbe::ObjectPath(world), state->MatchmakerHuntID.ToString(),
        DojoProbe::ObjectPath(state->GameModeClass.Get()));
}
inline std::vector<Creature> Catalog(UWorld* world) {
    std::vector<Creature> result;
    auto* instance = world ? UGameplayStatics::GetGameInstance(world) : nullptr;
    if (!instance || !instance->IsA(UArchonGameInstance::StaticClass())) return result;
    auto* game = static_cast<UArchonGameInstance*>(instance);
    auto* table = game->BehemothData;
    if (!table || !table->RowStruct || table->RowStruct->GetName() != "BehemothInfo"
        || table->RowMap.NumAllocated() < 0 || table->RowMap.NumAllocated() > 1024) return result;
    for (int i = 0; i < table->RowMap.NumAllocated(); ++i) if (table->RowMap.IsValidIndex(i)) {
        auto* row = reinterpret_cast<FBehemothInfo*>(table->RowMap[i].Value());
        if (!row) continue;
        const auto id = table->RowMap[i].Key().ToString();
        const auto path = row->BehemothAsset.ObjectID.AssetPathName.GetRawString();
        if (!path.starts_with("/Game/Monsters/") || !path.ends_with("_C")
            || !TestArenaPolicy::Parse(std::string(TestArenaPolicy::Prefix) + "spawn|" + id)) continue;
        const auto name = row->DisplayName.TextData ? row->DisplayName.ToString() : id;
        result.push_back({id,TestArenaPolicy::DisplayName(id,name.empty() ? "Unnamed creature" : name),path});
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.Label == b.Label ? a.Id < b.Id : a.Label < b.Label;});
    return result;
}
inline FText Text(const std::wstring& text) { return UKismetTextLibrary::Conv_StringToText(FString(text.c_str())); }
inline FText Text(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring wide(length,L'\0');
    if (length) MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),wide.data(),length);
    return Text(wide);
}
inline void Reply(AArchonPlayerController* player, const std::string& message) {
    const auto text = std::string(TestArenaPolicy::Prefix) + message;
    const std::wstring wide(text.begin(),text.end());
    player->ClientMessage(FString(wide.c_str()), UKismetStringLibrary::Conv_StringToName(FString(L"TestArena144")), 5);
}
inline TArray<AActor*> Creatures(UWorld* world) {
    TArray<AActor*> actors;
    UGameplayStatics::GetAllActorsOfClass(world,AArchonBehemoth::StaticClass(),&actors);
    return actors;
}
inline bool Spawn(UWorld* world,AArchonPlayerController* player,const Creature& creature,std::string& result) {
    auto* mode = FrostEncounter::IslandMode(world);
    auto* state = FrostEncounter::State(world);
    auto* pawn = player->Pawn;
    if (!mode || !state || !pawn || !state->GameStarted || pawn->IsActorBeingDestroyed()) { result="Wait until you have landed in the arena."; return false; }
    const auto actors = Creatures(world);
    if (actors.Num() < 0 || actors.Num() > 128) { result="Creature list unavailable."; return false; }
    int living=0;
    for (auto* actor : actors) if (actor && !actor->IsActorBeingDestroyed() && static_cast<AArchonBehemoth*>(actor)->GetCurrentHealth()>0) ++living;
    if (living >= TestArenaPolicy::MaxLiving) { result="Three creatures are already alive. Despawn one first."; return false; }
    auto* spawnerType=Ubp_gamemode_behemoth_spawner_component_C::StaticClass();
    auto* component=mode->GetComponentByClass(spawnerType);
    auto* spawn=DojoProbe::FindFunction(component,"SpawnBehemothAtLocation");
    if (!component || !spawn) { result="The arena spawner is not ready."; return false; }
    const std::wstring path(creature.Path.begin(),creature.Path.end());
    auto* type=UKismetSystemLibrary::LoadClassAsset_Blocking(UKismetSystemLibrary::Conv_SoftClassPathToSoftClassRef(UKismetSystemLibrary::MakeSoftClassPath(path.c_str())));
    bool valid=false;
    for (auto* parent=static_cast<UStruct*>(type);parent;parent=parent->SuperStruct) if (parent==AArchonBehemoth::StaticClass()) { valid=true; break; }
    if (!valid) { result="This creature is listed but its combat class is unavailable in this installation."; return false; }
    const auto origin=pawn->K2_GetActorLocation(), forward=pawn->GetActorForwardVector();
    const FVector desired{origin.X+forward.X*1600,origin.Y+forward.Y*1600,origin.Z};
    TArray<AActor*> ignore=actors; ignore.Add(pawn);
    FHitResult hit{};
    const bool ground=UKismetSystemLibrary::LineTraceSingle(world,{desired.X,desired.Y,desired.Z+800},{desired.X,desired.Y,desired.Z-3000},
        ETraceTypeQuery::TraceTypeQuery1,false,ignore,EDrawDebugTrace::None,&hit,true,{},{},0)
        && hit.bBlockingHit && !hit.bStartPenetrating && hit.ImpactNormal.Z>=0.55f;
    FVector location{};
    if (!ground || !UNavigationSystemV1::K2_ProjectPointToNavigation(world,hit.ImpactPoint,&location,nullptr,{},FVector{500,500,800})) {
        result="Face a clear area inside the arena and try again."; return false;
    }
    auto* spawner=static_cast<Ubp_gamemode_behemoth_spawner_component_C*>(component);
    spawner->EnableBehemothIntros=false;
    spawner->SpawnedBehemothsShouldEndMission=false;
    Params::bp_gamemode_behemoth_spawner_component_C_SpawnBehemothAtLocation request{};
    request.MonsterClass=type; request.Location=location; request.Rotation=pawn->K2_GetActorRotation(); request.Rotation.Yaw+=180;
    request.TryToAlignToGround=true;
    request.SpawnParams.IsTemporarySpawnedBehemoth_5_22372EA1469479FC8833F3A5F783A857=true;
    request.SpawnParams.DelayAfterDeathBeforeDissolve_12_BF65FBEA4583569FF6942D9D01DC13DD=5;
    component->ProcessEvent(spawn,&request);
    auto* monster=request.Behemoth;
    if (!monster) { result="The creature could not be spawned."; return false; }
    // The generic pursuit spawner has a single-value power override. Arena
    // creatures always start at their own authored asymmetric class defaults.
    auto* defaults=static_cast<AArchonBehemoth*>(type->ClassDefaultObject);
    if (defaults) {
        monster->GlobalPower=defaults->GlobalPower; monster->GlobalResistance=defaults->GlobalResistance;
        monster->UpdateBasePowers(); monster->UpdateBaseResists(true);
    }
    if (!FrostEncounter::Contains(mode->SpawnedBehemoths,monster)) {
        if (auto* registration=DojoProbe::FindFunction(mode,"RegisterBehemothSpawned")) {
            struct { AArchonBehemoth* Monster; } parameters{monster}; mode->ProcessEvent(registration,&parameters);
        }
    }
    if (!FrostEncounter::Contains(state->SpawnedBehemoths,monster)) {
        if (auto* tracking=DojoProbe::FindFunction(state,"TrackBehemothSpawn")) {
            struct { AArchonBehemoth* Monster; } parameters{monster}; state->ProcessEvent(tracking,&parameters);
        }
    }
    monster->ForceNetUpdate(); result="Creature spawned."; return true;
}
inline int Despawn(UWorld* world,const std::string& path) {
    auto actors=Creatures(world);
    if (actors.Num()<0 || actors.Num()>128) return 0;
    auto* mode=FrostEncounter::IslandMode(world); auto* state=FrostEncounter::State(world);
    int count=0;
    const auto remove=[](auto& array,AArchonBehemoth* monster) {
        for(int i=array.Num()-1;i>=0;--i) if(array[i]==monster) array.Remove(i);
    };
    for(auto* actor:actors) if(actor && actor->HasAuthority() && !actor->IsActorBeingDestroyed()
        && (path.empty() || DojoProbe::ObjectPath(actor->Class)==path)) {
        auto* monster=static_cast<AArchonBehemoth*>(actor);
        // Destroy rather than kill: no death rewards or hunt completion.
        actor->K2_DestroyActor();
        if(mode) remove(mode->SpawnedBehemoths,monster);
        if(state) remove(state->SpawnedBehemoths,monster);
        ++count;
    }
    if(state) state->ForceNetUpdate();
    return count;
}
inline bool BeforeServerEvent(UObject* object,UFunction* function,void* parameters,UNetDriver* driver) {
    if(!object || !function) return false;
    static std::map<UFunction*,bool> relevant;
    auto [event,inserted]=relevant.try_emplace(function,false);
    if(inserted) {
        const auto name=function->GetName();
        event->second=name=="ServerCheat" || name=="SpawnBehemoth" || name=="AddLootToPlayersForBehemoth"
            || name=="StartEndOfMatchCeremony" || name=="End_Of_Match_Ceremony";
    }
    if(!event->second) return false;
    auto* world=UWorld::GetWorld();
    if(!IsWorld(world) || !driver || world->NetDriver!=driver || driver->ServerConnection || !world->AuthorityGameMode) return false;
    const auto name=function->GetName();
    if(name=="SpawnBehemoth" && (object==world->AuthorityGameMode || object->IsA(Ubp_gamemode_behemoth_spawner_component_C::StaticClass()))) return true;
    if(object==world->AuthorityGameMode && (name=="AddLootToPlayersForBehemoth" || name=="StartEndOfMatchCeremony" || name=="End_Of_Match_Ceremony")) return true;
    if(name!="ServerCheat" || !parameters || !object->IsA(AArchonPlayerController::StaticClass())) return false;
    auto* player=static_cast<AArchonPlayerController*>(object);
    bool ownsConnection=false;
    for(auto* connection:driver->ClientConnections) if(connection && connection->PlayerController==player) ownsConnection=true;
    if(!ownsConnection || !player->HasAuthority()) return true;
    const auto& input=static_cast<Params::ArchonPlayerController_ServerCheat*>(parameters)->Params_0;
    if(!input.IsValid() || input.Num()<1 || input.Num()>181) return true;
    const auto command=TestArenaPolicy::Parse(input.ToString());
    if(!command) return true; // Never forward a general cheat command.
    static UWorld* previous=nullptr; static ULONGLONG next=0;
    if(previous!=world) {previous=world;next=0;}
    if(GetTickCount64()<next) {Reply(player,"Please wait before the next action.");return true;}
    next=GetTickCount64()+500;
    if(command->Operation==TestArenaPolicy::Action::Clear) {Reply(player,"Cleared "+std::to_string(Despawn(world,{}))+" creature(s).");return true;}
    const auto catalog=Catalog(world);
    const auto selected=std::find_if(catalog.begin(),catalog.end(),[&](const auto& entry){return entry.Id==command->Id;});
    if(selected==catalog.end()) {Reply(player,"This creature is not in the installed catalog.");return true;}
    if(command->Operation==TestArenaPolicy::Action::Despawn) Reply(player,"Despawned "+std::to_string(Despawn(world,selected->Path))+" creature(s).");
    else {std::string result; Spawn(world,player,*selected,result);Reply(player,result);}
    return true;
}
}
