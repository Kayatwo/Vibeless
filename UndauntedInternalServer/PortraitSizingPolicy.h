#pragma once

namespace PortraitSizingPolicy {
struct Bounds { float MinX, MinY, MaxX, MaxY, TextureSize; };
struct Region { float MinX, MinY, MaxX, MaxY; };

constexpr float Extent(const Bounds& bounds) {
    const float width = bounds.MaxX - bounds.MinX, height = bounds.MaxY - bounds.MinY;
    return (width > height ? width : height) / bounds.TextureSize;
}
// Match the original portrait's longest visible dimension and visual center.
// A square UV window preserves aspect ratio and leaves the stock HUD layout alone.
constexpr Region Fit(const Bounds& source, const Bounds& original) {
    const float span = Extent(source) / Extent(original);
    const float x = (source.MinX + source.MaxX) / (2 * source.TextureSize)
        - span * (original.MinX + original.MaxX) / (2 * original.TextureSize);
    const float y = (source.MinY + source.MaxY) / (2 * source.TextureSize)
        - span * (original.MinY + original.MaxY) / (2 * original.TextureSize);
    return {x, y, x + span, y + span};
}

// Alpha >= 16/255 bounds measured from the exact mounted BC7 mip bytes.
// Original references are the 1.4.4 Gnasher/Drask icons used by these HUD slots.
// Evidence and source hashes: Audit/2026-10-18/portrait-sizing/measurements.json.
inline constexpr Bounds FrostArtwork{75, 76, 401, 406, 512};
inline constexpr Bounds OriginalGnasher{33, 49, 214, 196, 256};
inline constexpr Bounds DraskArtwork{109, 84, 400, 404, 512};
inline constexpr Bounds OriginalDrask{31, 29, 214, 228, 256};
inline constexpr Region Frost = Fit(FrostArtwork, OriginalGnasher);
inline constexpr Region Drask = Fit(DraskArtwork, OriginalDrask);

constexpr bool Near(float a, float b) { return a >= b - 0.000001f && a <= b + 0.000001f; }
template<class Brush> bool Matches(const Brush& brush, const Region& region) {
    const auto& uv = brush.UVRegion;
    return uv.bIsValid && Near(uv.Min.X, region.MinX) && Near(uv.Min.Y, region.MinY)
        && Near(uv.Max.X, region.MaxX) && Near(uv.Max.Y, region.MaxY);
}
// Called only after the existing discovery guard permits portrait replacement.
// Set absolute UVs so repeated polling/native brush resets cannot accumulate zoom.
template<class Brush> void Apply(Brush& brush, const Region& region) {
    auto& uv = brush.UVRegion;
    uv = {};
    uv.Min.X = region.MinX; uv.Min.Y = region.MinY;
    uv.Max.X = region.MaxX; uv.Max.Y = region.MaxY;
    uv.bIsValid = 1;
}
}
