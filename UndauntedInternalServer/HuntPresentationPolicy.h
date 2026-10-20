// Hunt menu recommendations for registered hunts (ContentRegistry.h). AGPL-3.0-only.
#pragma once
#include "ContentRegistry.h"
#include "OriginalStrengthPolicy.h"

namespace HuntPresentationPolicy {
// An explicit match takes precedence over the broader player-hunt selection.
inline std::optional<OriginalStrengthPolicy::Stats> ForHunt(const ContentRegistry::Registry& registry,
    std::string_view player, std::string_view match) {
    return registry.PresentedFor(player, match);
}
using ContentRegistry::Recommended;
using ContentRegistry::Targets;
}
