// Bind the separately mounted Deepfrost portrait to its existing stock HUD slot. AGPL-3.0-only.
#pragma once
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include "FeatureSupport.h"
#include "PortraitDiscoveryPolicy.h"
#include "PortraitSizingPolicy.h"
#include "SDK/UMG_classes.hpp"
#include "SDK/UMG_parameters.hpp"

namespace FrostIcon {
using namespace SDK;
inline constexpr wchar_t TexturePath[] = L"/Game/UI/Textures/Icons/Behemoths/New_Sigils/illustrated/ico_frost_gnasher_512x512.ico_frost_gnasher_512x512";
inline UTexture2D* Texture = nullptr;
inline int32 TextureIndex = -1;
inline int LoadAttempts = 0;
inline ULONGLONG NextLoad = 0;
inline bool MissingReported = false;
inline std::set<std::string> Reported;

inline void Log(const std::string& body) {
    std::ofstream out(std::filesystem::path(FeatureSupport::Root()) / L"reports/frost-icon.jsonl", std::ios::app);
    out << "{\"pid\":" << GetCurrentProcessId() << ",\"uptimeMs\":" << GetTickCount64() << ',' << body << "}\n";
}
inline bool IsFrost(AArchonBehemoth* behemoth) {
    if (!behemoth || behemoth->IsActorBeingDestroyed() || !behemoth->Class) return false;
    auto* package = behemoth->Class->Outer;
    while (package && package->Outer) package = package->Outer;
    const auto path = package ? package->Name.GetRawString() : std::string{};
    return behemoth->Class->GetName() == "mcbeaver_frost_bp_C"
        && (path == "/Game/Monsters/mcrollin/mcbeaver_frost_bp" || path == "/Game/Monsters/frost144/mcbeaver_frost_bp");
}
inline UTexture2D* Load() {
    if (Texture && TextureIndex >= 0 && UObject::GObjects->GetByIndex(TextureIndex) == Texture) return Texture;
    Texture = nullptr;
    if (LoadAttempts >= 3 || GetTickCount64() < NextLoad) return nullptr;
    std::error_code ec;
    const auto pak = std::filesystem::path(FeatureSupport::Root()) / L"game/Archon/Content/Paks/Deepfrost_Portrait_P.pak";
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
        + ",\"loaded\":" + FeatureSupport::Bool(texture != nullptr) + ",\"width\":" + std::to_string(width)
        + ",\"height\":" + std::to_string(height) + ",\"valid\":" + FeatureSupport::Bool(valid)
        + ",\"asset\":" + (asset ? FeatureSupport::Json(asset->GetFullName()) : "null"));
    if (!valid) return nullptr;
    Texture = texture; TextureIndex = texture->Index;
    LoadAttempts = 0; // Successful loads must not exhaust retries across later hunts/GC.
    return Texture;
}
// Invoke for each already bound frost slot after TickClient's stock HUD notification.
// Keep calling on later ticks: missing packages/slots never block normal HUD setup.
inline bool Apply(UHUDBehemothStateWidget* slot) {
    auto* world = UWorld::GetWorld();
    if (!FeatureSupport::IsExpectedProcess() || !world || !world->NetDriver || !world->NetDriver->ServerConnection
        || world->AuthorityGameMode || !slot || !slot->BehemothIcon) return false;
    auto* behemoth = slot->Behemoth.Get();
    if (!IsFrost(behemoth) || behemoth->GetCurrentHealth() <= 0) return false;
    auto* image = slot->BehemothIcon;
    const auto white = [](const FLinearColor& color) { return PortraitDiscoveryPolicy::IsWhite(color); };
    if (!PortraitDiscoveryPolicy::CanReplace(behemoth->bImageRevealed, image->Brush.ResourceObject != nullptr,
        image->RenderOpacity, image->Visibility == ESlateVisibility::Hidden || image->Visibility == ESlateVisibility::Collapsed,
        image->Brush.TintColor.ColorUseRule == ESlateColorStylingMode::UseColor_Specified,
        white(image->Brush.TintColor.SpecifiedColor), white(image->ColorAndOpacity))) return false;
    auto* texture = Load();
    if (!texture) return false;
    const auto key = std::to_string(slot->Index) + ":" + std::to_string(behemoth->Index);
    const bool first = Reported.find(key) == Reported.end();
    const auto& prior = image->Brush;
    if (first) Log("\"event\":\"portrait_before_repair\",\"monsterIndex\":" + std::to_string(behemoth->Index)
        + ",\"imageRevealed\":" + FeatureSupport::Bool(behemoth->bImageRevealed)
        + ",\"uvValid\":" + FeatureSupport::Bool(prior.UVRegion.bIsValid != 0)
        + ",\"uvMinX\":" + std::to_string(prior.UVRegion.Min.X) + ",\"uvMinY\":" + std::to_string(prior.UVRegion.Min.Y)
        + ",\"uvMaxX\":" + std::to_string(prior.UVRegion.Max.X) + ",\"uvMaxY\":" + std::to_string(prior.UVRegion.Max.Y)
        + ",\"brushTintWhite\":" + FeatureSupport::Bool(white(prior.TintColor.SpecifiedColor))
        + ",\"imageColorWhite\":" + FeatureSupport::Bool(white(image->ColorAndOpacity))
        + ",\"renderOpacity\":" + std::to_string(image->RenderOpacity)
        + ",\"visibility\":" + std::to_string(static_cast<int>(image->Visibility)));
    // Reusing the old portrait brush can retain atlas UVs, silhouette tint or tiling.
    // Build a native brush with measured padding compensation; keep the stock slot size.
    if (first || prior.ResourceObject != texture || !PortraitSizingPolicy::Matches(prior, PortraitSizingPolicy::Frost)
        || prior.DrawAs != ESlateBrushDrawType::Image || prior.Tiling != ESlateBrushTileType::NoTile
        || prior.Mirroring != ESlateBrushMirrorType::NoMirror
        || prior.TintColor.ColorUseRule != ESlateColorStylingMode::UseColor_Specified
        || !white(prior.TintColor.SpecifiedColor)) {
        auto* type = UObject::FindClassFast("WidgetBlueprintLibrary");
        auto* library = type ? type->ClassDefaultObject : nullptr;
        auto* make = FeatureSupport::FindFunction(library, "MakeBrushFromTexture");
        auto* set = FeatureSupport::FindFunction(image, "SetBrush");
        if (!library || !make || !set) return false;
        Params::WidgetBlueprintLibrary_MakeBrushFromTexture params{};
        params.Texture = texture;
        params.Width = prior.ImageSize.X > 0 && prior.ImageSize.X <= 4096 ? static_cast<int32>(prior.ImageSize.X) : 512;
        params.Height = prior.ImageSize.Y > 0 && prior.ImageSize.Y <= 4096 ? static_cast<int32>(prior.ImageSize.Y) : 512;
        library->ProcessEvent(make, &params);
        PortraitSizingPolicy::Apply(params.ReturnValue, PortraitSizingPolicy::Frost);
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
        if (auto* function = FeatureSupport::FindFunction(image, "SetColorAndOpacity")) image->ProcessEvent(function, &color);
    }
    if (image->RenderOpacity != 1.0f) {
        Params::Widget_SetRenderOpacity opacity{};
        opacity.InOpacity = 1.0f;
        if (auto* function = FeatureSupport::FindFunction(image, "SetRenderOpacity")) image->ProcessEvent(function, &opacity);
    }
    const auto visibility = image->Visibility;
    if (visibility == ESlateVisibility::Hidden || visibility == ESlateVisibility::Collapsed) {
        Params::Widget_SetVisibility visible{};
        visible.InVisibility = ESlateVisibility::HitTestInvisible;
        if (auto* function = FeatureSupport::FindFunction(image, "SetVisibility")) image->ProcessEvent(function, &visible);
    }
    const bool sizedToStock = PortraitSizingPolicy::Matches(image->Brush, PortraitSizingPolicy::Frost);
    const bool applied = image->Brush.ResourceObject == texture && sizedToStock
        && white(image->Brush.TintColor.SpecifiedColor) && white(image->ColorAndOpacity)
        && image->RenderOpacity == 1.0f;
    if (Reported.insert(key).second) Log("\"event\":\"portrait_bound\",\"monsterIndex\":" + std::to_string(behemoth->Index)
        + ",\"slot\":" + FeatureSupport::Json(slot->GetName()) + ",\"applied\":" + FeatureSupport::Bool(applied)
        + ",\"fullTextureBrush\":" + FeatureSupport::Bool(!image->Brush.UVRegion.bIsValid)
        + ",\"sizedToStock\":" + FeatureSupport::Bool(sizedToStock)
        + ",\"uvMinX\":" + std::to_string(image->Brush.UVRegion.Min.X) + ",\"uvMinY\":" + std::to_string(image->Brush.UVRegion.Min.Y)
        + ",\"uvMaxX\":" + std::to_string(image->Brush.UVRegion.Max.X) + ",\"uvMaxY\":" + std::to_string(image->Brush.UVRegion.Max.Y)
        + ",\"imageRevealed\":" + FeatureSupport::Bool(behemoth->bImageRevealed));
    return applied;
}
}
