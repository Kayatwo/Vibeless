// Keep source-derived float operation ordering isolated from other features.
// This file is compiled with /fp:strict; no fast-math/contraction is permitted.
#include "ThraxTrackingKernel.h"
#if VIBELESS_FEATURES
namespace ThraxTrackingKernel {
feareater_tracking::Result Solve(const feareater_tracking::Input& input) { return feareater_tracking::solve(input); }
feareater_tracking::Transform Relative(const feareater_tracking::Transform& child, const feareater_tracking::Transform& parent) {
    return feareater_tracking::relative(child, parent);
}
}
#endif
