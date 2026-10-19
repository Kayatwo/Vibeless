#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include "../PortraitSizingPolicy.h"
#include "../SDK/SlateCore_structs.hpp"

void CheckProfile(const PortraitSizingPolicy::Region& region,
    const PortraitSizingPolicy::Bounds& source, const PortraitSizingPolicy::Bounds& original,
    float expectedCoverage, float expectedScale) {
    using namespace PortraitSizingPolicy;
    const float width = region.MaxX - region.MinX, height = region.MaxY - region.MinY;
    assert(Near(width, height)); // No stretching.
    assert(region.MinX >= 0 && region.MinY >= 0 && region.MaxX <= 1 && region.MaxY <= 1);
    assert(Near(1 / width, expectedScale));
    const float left = (source.MinX / source.TextureSize - region.MinX) / width;
    const float right = (source.MaxX / source.TextureSize - region.MinX) / width;
    const float top = (source.MinY / source.TextureSize - region.MinY) / height;
    const float bottom = (source.MaxY / source.TextureSize - region.MinY) / height;
    assert(left > 0 && right < 1 && top > 0 && bottom < 1); // No visible-art clipping.
    assert(Near((std::max)(right - left, bottom - top), expectedCoverage));
    assert(Near((left + right) / 2, (original.MinX + original.MaxX) / (2 * original.TextureSize)));
    assert(Near((top + bottom) / 2, (original.MinY + original.MaxY) / (2 * original.TextureSize)));

    SDK::FSlateBrush brush{};
    brush.ImageSize.X = 128; brush.ImageSize.Y = 96;
    brush.TintColor.SpecifiedColor.R = 0.2f; brush.TintColor.SpecifiedColor.A = 0.4f;
    brush.DrawAs = SDK::ESlateBrushDrawType::Box;
    brush.Margin.Left = 0.3f;
    brush.ResourceObject = reinterpret_cast<SDK::UObject*>(0x1234);
    // SDK copy constructors do not promise to preserve unused struct padding.
    // Snapshot the actual object bytes when checking for unrelated writes.
    std::array<unsigned char, sizeof(brush)> originalBytes{}, fittedBytes{};
    std::memcpy(originalBytes.data(), &brush, sizeof(brush));
    assert(!Matches(brush, region));
    Apply(brush, region);
    assert(Matches(brush, region));
    std::memcpy(fittedBytes.data(), &brush, sizeof(brush));
    const auto fittedUV = brush.UVRegion;
    // Apply is absolute, including after a stale atlas UV/native brush reset.
    for (int tick = 0; tick < 2000; ++tick) {
        if (tick % 2) { brush.UVRegion.Min.X = 0.7f; brush.UVRegion.Max.Y = 0.8f; }
        Apply(brush, region);
        assert(Matches(brush, region));
        assert(std::memcmp(&brush, fittedBytes.data(), sizeof(brush)) == 0);
    }
    std::memcpy(&brush.UVRegion, originalBytes.data() + offsetof(SDK::FSlateBrush, UVRegion), sizeof(brush.UVRegion));
    assert(std::memcmp(&brush, originalBytes.data(), sizeof(brush)) == 0); // UV is the only changed field.
    brush.UVRegion = fittedUV;
    brush.UVRegion.Min.X += 0.01f;
    assert(!Matches(brush, region));
    brush.UVRegion = fittedUV;
    brush.UVRegion.bIsValid = 0;
    assert(!Matches(brush, region));
}

int main() {
    using namespace PortraitSizingPolicy;
    CheckProfile(Frost, FrostArtwork, OriginalGnasher, 181.0f / 256, 362.0f / 330);
    CheckProfile(Drask, DraskArtwork, OriginalDrask, 199.0f / 256, 398.0f / 320);
    std::cout << "PASS portrait sizing: both stock references, aspect/center/clipping, SDK brush preservation, 4000 polling/reset cycles\n";
}
