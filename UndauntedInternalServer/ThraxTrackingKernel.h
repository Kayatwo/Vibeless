// Strict-floating-point translation-unit boundary. AGPL-3.0-only.
#pragma once
// Windows RPC defines "small" as a macro; keep the portable kernel byte-identical.
#pragma push_macro("small")
#pragma push_macro("min")
#pragma push_macro("max")
#undef small
#undef min
#undef max
#include "ThraxTrackingKernel/tracking_solver.hpp"
#pragma pop_macro("max")
#pragma pop_macro("min")
#pragma pop_macro("small")
namespace ThraxTrackingKernel {
feareater_tracking::Result Solve(const feareater_tracking::Input& input);
feareater_tracking::Transform Relative(const feareater_tracking::Transform& child, const feareater_tracking::Transform& parent);
}
