#pragma once
#include "HuntPresentationPolicy.h"
#include "DojoProbe.h"
#include "SDK/huntinfo_current_loadout_rec_bpw_parameters.hpp"
#include "SDK/hunt_function_library_parameters.hpp"

namespace HuntPresentation {
using namespace SDK;
inline FText Text(const std::wstring& value) { return UKismetTextLibrary::Conv_StringToText(FString(value.c_str())); }
inline void AfterEvent(UObject* object, UFunction* function, void* parameters) {
    if (!DojoProbe::IsExpectedProcess() || !object || !function) return;
    const auto name = function->GetName();
    if (parameters && name == "GetAverageBehemothPowerForHunt"
        && function->Outer && function->Outer->GetName() == "hunt_function_library_C") {
        auto& query = *static_cast<Params::hunt_function_library_C_GetAverageBehemothPowerForHunt*>(parameters);
        if (const auto stats = HuntPresentationPolicy::ForHunt(query.PlayerHuntId.ToString(), query.MatchmakerHuntID.ToString()))
            query.AveragePower = stats->Power;
    }
    if (parameters && name == "UpdateRecommendedStats" && object->IsA(Uhuntinfo_current_loadout_rec_bpw_C::StaticClass())) {
        const auto& query = *static_cast<Params::huntinfo_current_loadout_rec_bpw_C_UpdateRecommendedStats*>(parameters);
        const auto stats = HuntPresentationPolicy::ForHunt(query.PlayerHuntId.ToString(), query.MatchmakerHuntID.ToString());
        if (!stats) return;
        auto* widget = static_cast<Uhuntinfo_current_loadout_rec_bpw_C*>(object);
        const auto targets = HuntPresentationPolicy::Recommended(*stats);
        if (widget->WeaponTargetTextBlock) widget->WeaponTargetTextBlock->SetText(Text(std::to_wstring(static_cast<int>(targets.Weapon))));
        if (widget->ArmourTargetTextBlock) widget->ArmourTargetTextBlock->SetText(Text(std::to_wstring(static_cast<int>(targets.Armour))));
        if (query.PlayerLoadout) {
            Params::huntinfo_current_loadout_rec_bpw_C_GetBehemothElement element{};
            element.PlayerHuntId = query.PlayerHuntId;
            element.MatchmakerHuntID = query.MatchmakerHuntID;
            if (auto* getElement = DojoProbe::FindFunction(widget, "GetBehemothElement")) {
                widget->ProcessEvent(getElement, &element);
                const auto colour = [](bool ready) {
                    FSlateColor result{};
                    result.SpecifiedColor = ready ? FLinearColor{1,1,1,1} : FLinearColor{1,0.15f,0.15f,1};
                    return result;
                };
                if (widget->WeaponCurrentTextBlock) widget->WeaponCurrentTextBlock->SetColorAndOpacity(colour(
                    query.PlayerLoadout->GetTotalWeaponPowerAgainstBehemoth(element.BehemothWeakness, element.BehemothResistance) >= targets.Weapon));
                if (widget->ArmourCurrentTextBlock) widget->ArmourCurrentTextBlock->SetColorAndOpacity(colour(
                    query.PlayerLoadout->GetTotalArmourResistanceAgainstBehemoth(element.BehemothWeakness, element.BehemothResistance) >= targets.Armour));
            }
        }
    }
    if ((name == "OnViewModelUpdated" || name == "UpdateViewModelFromPlayerHuntID")
        && object->IsA(UHuntInfoPageWidget::StaticClass())) {
        auto* page = static_cast<UHuntInfoPageWidget*>(object);
        if (const auto stats = HuntPresentationPolicy::ForHunt(page->MapItemViewModel.PlayerHuntId.ToString(), "")) {
            const auto targets = HuntPresentationPolicy::Recommended(*stats);
            const auto label = Text(L"Weapon " + std::to_wstring(static_cast<int>(targets.Weapon))
                + L" / Armour " + std::to_wstring(static_cast<int>(targets.Armour)));
            page->MapItemViewModel.RecommendedPowerMessage = label;
            if (page->RecommendedPowerTextBlock) page->RecommendedPowerTextBlock->SetText(label);
        }
    }
}
inline void Tick(UWorld* world) {
    static ULONGLONG next=0;
    if(!DojoProbe::IsExpectedProcess() || !world || GetTickCount64()<next) return;
    next=GetTickCount64()+100;
    auto* panelType=UObject::FindClassFast("huntinfo_current_loadout_rec_bpw_C");
    if(!panelType) return;
    TArray<UUserWidget*> pages;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(world,&pages,UHuntInfoPageWidget::StaticClass(),false);
    if(pages.Num()<0 || pages.Num()>64) return;
    for(auto* candidate:pages) {
        if(!candidate || candidate->IsDefaultObject()) continue;
        auto* page=static_cast<UHuntInfoPageWidget*>(candidate);
        const auto id=page->MapItemViewModel.PlayerHuntId.ToString();
        if(!HuntPresentationPolicy::ForHunt(id,"")) continue;
        AfterEvent(page,DojoProbe::FindFunction(page,"OnViewModelUpdated"),nullptr);
        // Blueprint-to-Blueprint calls can bypass UObject::ProcessEvent. Also
        // correct the page's bound recommendation widget from its current ID.
        auto* panel=page->RecommendedPowerPanel;
        if(!panel || !panel->IsA(panelType)) continue;
        Params::huntinfo_current_loadout_rec_bpw_C_UpdateRecommendedStats query{};
        const std::wstring wide(id.begin(),id.end());
        query.PlayerHuntId=FString(wide.c_str());query.MatchmakerHuntID=FString(L"");query.PlayerLoadout=page->PlayerLoadoutViewModel;
        AfterEvent(panel,DojoProbe::FindFunction(panel,"UpdateRecommendedStats"),&query);
    }
}
}
