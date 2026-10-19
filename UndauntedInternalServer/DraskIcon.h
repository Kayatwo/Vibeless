// Bind the isolated modern Thunderdeep portrait to its existing stock HUD slot. AGPL-3.0-only.
#pragma once
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include "DojoProbe.h"
#include "PortraitDiscoveryPolicy.h"
#include "PortraitSizingPolicy.h"
#include "SDK/UMG_classes.hpp"
#include "SDK/UMG_parameters.hpp"

namespace DraskIcon {
using namespace SDK;
inline constexpr wchar_t TexturePath[] = L"/Game/Monsters/drask144/ui/ico_thunderdeep_drask_512x512.ico_thunderdeep_drask_512x512";
inline UTexture2D* Texture = nullptr;
inline int32 TextureIndex = -1;
inline int LoadAttempts = 0;
inline ULONGLONG NextLoad = 0;
inline bool MissingReported = false;
inline std::set<std::string> Reported;
inline std::set<std::string> Registered;
inline UWorld* LastWorld = nullptr;

inline void Log(const std::string& body) {
    static unsigned count = 0;
    if (count++ >= 512) return;
    std::ofstream out(std::filesystem::path(DojoProbe::Root()) / L"reports/drask-icon.jsonl", std::ios::app);
    out << "{\"pid\":" << GetCurrentProcessId() << ",\"uptimeMs\":" << GetTickCount64() << ',' << body << "}\n";
}
inline bool IsDrask(AArchonBehemoth* behemoth) {
    return behemoth && !behemoth->IsActorBeingDestroyed() && behemoth->Class
        && DojoProbe::ObjectPath(behemoth->Class) == "/Game/Monsters/drask144/fulgar_alpha_bp.fulgar_alpha_bp_C";
}
inline UTexture2D* Load() {
    if (Texture && TextureIndex >= 0 && UObject::GObjects->GetByIndex(TextureIndex) == Texture) return Texture;
    Texture = nullptr;
    if (LoadAttempts >= 3 || GetTickCount64() < NextLoad) return nullptr;
    std::error_code ec;
    const auto pak = std::filesystem::path(DojoProbe::Root()) / L"game/Archon/Content/Paks/Thunderdeep144_Prototype_P.pak";
    if (!std::filesystem::is_regular_file(pak, ec) || ec) {
        if (!MissingReported) { Log("\"event\":\"portrait_package_missing\""); MissingReported = true; }
        return nullptr;
    }
    ++LoadAttempts;
    NextLoad = GetTickCount64() + 1000;
    const auto soft = UKismetSystemLibrary::MakeSoftObjectPath(TexturePath);
    auto* asset = UKismetSystemLibrary::LoadAsset_Blocking(UKismetSystemLibrary::Conv_SoftObjPathToSoftObjRef(soft));
    auto* type = UObject::FindClassFast("Texture2D");
    auto* texture = asset && type && asset->IsA(type) ? static_cast<UTexture2D*>(asset) : nullptr;
    const int32 width = texture ? texture->Blueprint_GetSizeX() : 0, height = texture ? texture->Blueprint_GetSizeY() : 0;
    const bool valid = texture && width == 512 && height == 512;
    Log("\"event\":\"portrait_load\",\"attempt\":" + std::to_string(LoadAttempts)
        + ",\"loaded\":" + DojoProbe::Bool(texture != nullptr) + ",\"width\":" + std::to_string(width)
        + ",\"height\":" + std::to_string(height) + ",\"valid\":" + DojoProbe::Bool(valid)
        + ",\"asset\":" + (asset ? DojoProbe::Json(asset->GetFullName()) : "null"));
    if (!valid) return nullptr;
    Texture = texture; TextureIndex = texture->Index;
    LoadAttempts = 0; // Successful loads must not exhaust retries across later hunts/GC.
    return Texture;
}
// Invoke for each already bound unique Drask slot after TickClient's stock HUD notification.
// Keep calling on later ticks: missing packages/slots never block normal HUD setup.
inline bool Apply(UHUDBehemothStateWidget* slot) {
    auto* world = UWorld::GetWorld();
    if (!DojoProbe::IsExpectedProcess() || !world || !world->NetDriver || !world->NetDriver->ServerConnection
        || world->AuthorityGameMode || !slot || !slot->BehemothIcon) return false;
    auto* behemoth = slot->Behemoth.Get();
    if (!IsDrask(behemoth) || behemoth->GetCurrentHealth() <= 0) return false;
    auto* image = slot->BehemothIcon;
    const auto white = [](const FLinearColor& color) { return PortraitDiscoveryPolicy::IsWhite(color); };
    // The old prototype HUD can already show a full-color stock portrait while
    // bImageRevealed remains false. Replace that visible presentation, while
    // leaving hidden or silhouette portraits to native discovery.
    const bool stockPortraitVisible = PortraitDiscoveryPolicy::CanReplace(false, image->Brush.ResourceObject != nullptr,
        image->RenderOpacity, image->Visibility == ESlateVisibility::Hidden || image->Visibility == ESlateVisibility::Collapsed,
        image->Brush.TintColor.ColorUseRule == ESlateColorStylingMode::UseColor_Specified,
        white(image->Brush.TintColor.SpecifiedColor), white(image->ColorAndOpacity));
    if (!behemoth->bImageRevealed && !stockPortraitVisible) return false;
    auto* texture = Load();
    if (!texture) return false;
    const auto key = std::to_string(slot->Index) + ":" + std::to_string(behemoth->Index);
    const bool first = Reported.find(key) == Reported.end();
    const auto& prior = image->Brush;
    if (first) Log("\"event\":\"portrait_before_repair\",\"monsterIndex\":" + std::to_string(behemoth->Index)
        + ",\"imageRevealed\":" + DojoProbe::Bool(behemoth->bImageRevealed)
        + ",\"stockPortraitVisible\":" + DojoProbe::Bool(stockPortraitVisible)
        + ",\"uvValid\":" + DojoProbe::Bool(prior.UVRegion.bIsValid != 0)
        + ",\"uvMinX\":" + std::to_string(prior.UVRegion.Min.X) + ",\"uvMinY\":" + std::to_string(prior.UVRegion.Min.Y)
        + ",\"uvMaxX\":" + std::to_string(prior.UVRegion.Max.X) + ",\"uvMaxY\":" + std::to_string(prior.UVRegion.Max.Y)
        + ",\"brushTintWhite\":" + DojoProbe::Bool(white(prior.TintColor.SpecifiedColor))
        + ",\"imageColorWhite\":" + DojoProbe::Bool(white(image->ColorAndOpacity))
        + ",\"renderOpacity\":" + std::to_string(image->RenderOpacity)
        + ",\"visibility\":" + std::to_string(static_cast<int>(image->Visibility)));
    // Reusing the old portrait brush can retain atlas UVs, silhouette tint or tiling.
    // Build a native brush with measured padding compensation; keep the stock slot size.
    if (first || prior.ResourceObject != texture || !PortraitSizingPolicy::Matches(prior, PortraitSizingPolicy::Drask)
        || prior.DrawAs != ESlateBrushDrawType::Image || prior.Tiling != ESlateBrushTileType::NoTile
        || prior.Mirroring != ESlateBrushMirrorType::NoMirror
        || prior.TintColor.ColorUseRule != ESlateColorStylingMode::UseColor_Specified
        || !white(prior.TintColor.SpecifiedColor)) {
        auto* type = UObject::FindClassFast("WidgetBlueprintLibrary");
        auto* library = type ? type->ClassDefaultObject : nullptr;
        auto* make = DojoProbe::FindFunction(library, "MakeBrushFromTexture");
        auto* set = DojoProbe::FindFunction(image, "SetBrush");
        if (!library || !make || !set) return false;
        Params::WidgetBlueprintLibrary_MakeBrushFromTexture params{};
        params.Texture = texture;
        params.Width = prior.ImageSize.X > 0 && prior.ImageSize.X <= 4096 ? static_cast<int32>(prior.ImageSize.X) : 512;
        params.Height = prior.ImageSize.Y > 0 && prior.ImageSize.Y <= 4096 ? static_cast<int32>(prior.ImageSize.Y) : 512;
        library->ProcessEvent(make, &params);
        PortraitSizingPolicy::Apply(params.ReturnValue, PortraitSizingPolicy::Drask);
        params.ReturnValue.DrawAs = ESlateBrushDrawType::Image;
        params.ReturnValue.Tiling = ESlateBrushTileType::NoTile;
        params.ReturnValue.Mirroring = ESlateBrushMirrorType::NoMirror;
        params.ReturnValue.TintColor.SpecifiedColor.R = params.ReturnValue.TintColor.SpecifiedColor.G
            = params.ReturnValue.TintColor.SpecifiedColor.B = params.ReturnValue.TintColor.SpecifiedColor.A = 1.0f;
        params.ReturnValue.TintColor.ColorUseRule = ESlateColorStylingMode::UseColor_Specified;
        Params::Image_SetBrush brush{};
        brush.InBrush = params.ReturnValue;
        image->ProcessEvent(set, &brush);
    }
    // Presentation-only repair: do not reveal hunt discovery or change hunter data.
    if (first || !white(image->ColorAndOpacity)) {
        Params::Image_SetColorAndOpacity color{};
        color.InColorAndOpacity.R = color.InColorAndOpacity.G = color.InColorAndOpacity.B = color.InColorAndOpacity.A = 1.0f;
        if (auto* function = DojoProbe::FindFunction(image, "SetColorAndOpacity")) image->ProcessEvent(function, &color);
    }
    if (image->RenderOpacity != 1.0f) {
        Params::Widget_SetRenderOpacity opacity{};
        opacity.InOpacity = 1.0f;
        if (auto* function = DojoProbe::FindFunction(image, "SetRenderOpacity")) image->ProcessEvent(function, &opacity);
    }
    const auto visibility = image->Visibility;
    if (visibility == ESlateVisibility::Hidden || visibility == ESlateVisibility::Collapsed) {
        Params::Widget_SetVisibility visible{};
        visible.InVisibility = ESlateVisibility::HitTestInvisible;
        if (auto* function = DojoProbe::FindFunction(image, "SetVisibility")) image->ProcessEvent(function, &visible);
    }
    const bool sizedToStock = PortraitSizingPolicy::Matches(image->Brush, PortraitSizingPolicy::Drask);
    const bool applied = image->Brush.ResourceObject == texture && sizedToStock
        && white(image->Brush.TintColor.SpecifiedColor) && white(image->ColorAndOpacity)
        && image->RenderOpacity == 1.0f;
    if (Reported.insert(key).second) Log("\"event\":\"portrait_bound\",\"monsterIndex\":" + std::to_string(behemoth->Index)
        + ",\"slot\":" + DojoProbe::Json(slot->GetName()) + ",\"applied\":" + DojoProbe::Bool(applied)
        + ",\"fullTextureBrush\":" + DojoProbe::Bool(!image->Brush.UVRegion.bIsValid)
        + ",\"sizedToStock\":" + DojoProbe::Bool(sizedToStock)
        + ",\"uvMinX\":" + std::to_string(image->Brush.UVRegion.Min.X) + ",\"uvMinY\":" + std::to_string(image->Brush.UVRegion.Min.Y)
        + ",\"uvMaxX\":" + std::to_string(image->Brush.UVRegion.Max.X) + ",\"uvMaxY\":" + std::to_string(image->Brush.UVRegion.Max.Y)
        + ",\"imageRevealed\":" + DojoProbe::Bool(behemoth->bImageRevealed));
    return applied;
}
// Existing client-engine tick, exact lab and exact ported actor only.
inline void Tick(UWorld* world) {
    if (!DojoProbe::IsExpectedProcess() || !world || !world->NetDriver
        || !world->NetDriver->ServerConnection || world->AuthorityGameMode) return;
    static ULONGLONG next = 0;
    if (GetTickCount64() < next) return;
    next = GetTickCount64() + 500;
    if (LastWorld != world) {
        LastWorld = world;
        Reported.clear();
        Registered.clear();
    }
    TArray<AActor*> actors;
    UGameplayStatics::GetAllActorsOfClass(world, AArchonBehemoth::StaticClass(), &actors);
    if (actors.Num() < 0 || actors.Num() > 32) return;
    bool present = false;
    for (auto* actor : actors) if (IsDrask(static_cast<AArchonBehemoth*>(actor))) { present = true; break; }
    if (!present) return;
    auto* player = UGameplayStatics::GetPlayerController(world, 0);
    if (!player) return;
    auto* type = UObject::FindClassFast("WidgetBlueprintLibrary");
    auto* library = type ? type->ClassDefaultObject : nullptr;
    auto* function = DojoProbe::FindFunction(library, "GetAllWidgetsOfClass");
    if (!library || !function) return;
    Params::WidgetBlueprintLibrary_GetAllWidgetsOfClass query{};
    query.WorldContextObject = world;
    query.WidgetClass = UHUDBehemothHealthManagerWidget::StaticClass();
    query.TopLevelOnly = false;
    library->ProcessEvent(function, &query);
    if (query.FoundWidgets.Num() < 0 || query.FoundWidgets.Num() > 32) return;
    for (auto* widget : query.FoundWidgets) {
        if (!widget || DojoProbe::ActorOutput(widget, "GetOwningPlayer") != player) continue;
        auto* manager = static_cast<UHUDBehemothHealthManagerWidget*>(widget);
        if (!manager->BehemothStateWidget1 && !manager->BehemothStateWidget2 && !manager->BehemothStateWidget3) continue;
        const auto slotHas = [](UHUDBehemothStateWidget* slot, AArchonBehemoth* actor) {
            return slot && slot->Behemoth.Get() == actor;
        };
        // Prototype maps can omit the stock spawn notification for manually spawned
        // behemoths. Notify only this local player's HUD; discovery stays native.
        for (auto* actor : actors) {
            auto* behemoth = static_cast<AArchonBehemoth*>(actor);
            if (!IsDrask(behemoth) || behemoth->GetCurrentHealth() <= 0) continue;
            const auto key = std::to_string(manager->Index) + ":" + std::to_string(behemoth->Index);
            if (Registered.size() >= 512 || !Registered.insert(key).second) continue;
            const bool bound = slotHas(manager->BehemothStateWidget1, behemoth)
                || slotHas(manager->BehemothStateWidget2, behemoth) || slotHas(manager->BehemothStateWidget3, behemoth);
            if (!bound) manager->OnBehemothSpawned(behemoth);
            const bool boundAfter = slotHas(manager->BehemothStateWidget1, behemoth)
                || slotHas(manager->BehemothStateWidget2, behemoth) || slotHas(manager->BehemothStateWidget3, behemoth);
            Log("\"event\":\"hud_registration\",\"monsterIndex\":" + std::to_string(behemoth->Index)
                + ",\"alreadyBound\":" + DojoProbe::Bool(bound) + ",\"boundAfterNotification\":" + DojoProbe::Bool(boundAfter)
                + ",\"imageRevealed\":" + DojoProbe::Bool(behemoth->bImageRevealed));
        }
        Apply(manager->BehemothStateWidget1);
        Apply(manager->BehemothStateWidget2);
        Apply(manager->BehemothStateWidget3);
    }
}

}
