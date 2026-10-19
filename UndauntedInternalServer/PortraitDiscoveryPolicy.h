#pragma once

namespace PortraitDiscoveryPolicy {
template<class Color> constexpr bool IsWhite(const Color& color) {
    return color.R == 1.0f && color.G == 1.0f && color.B == 1.0f && color.A == 1.0f;
}

// The stock prototype HUD may show a full portrait before bImageRevealed updates.
// Otherwise discovery owns hidden, transparent and silhouette presentations.
constexpr bool CanReplace(bool revealed, bool hasTexture, float opacity,
    bool hiddenOrCollapsed, bool specifiedColor, bool whiteBrush, bool whiteImage) {
    return revealed || (hasTexture && opacity == 1.0f && !hiddenOrCollapsed
        && specifiedColor && whiteBrush && whiteImage);
}
}
