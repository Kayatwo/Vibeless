// Compile the same strict-FP wrapper used by the DLL, without engine dependencies.
#define VIBELESS_FEATURES 1
#include "../ThraxTrackingKernel.cpp"
#include "../ThraxTrackingRuntimePolicy.h"
#include <cstdlib>
#include <iostream>
namespace Math = feareater_tracking;
using namespace ThraxTrackingRuntime;
static void Require(bool value) { if (!value) std::abort(); }
int main() {
    State state;
    auto token = state.Initialize({1, 2, 3, 4, 1001}).value();
    Snapshot snapshot;
    snapshot.Target = {7, 100, 50}; snapshot.Alpha = .25f; snapshot.Delta = .5f; snapshot.Stamp = {123, 9};
    Math::Input input;
    for (size_t i = 0; i < 7; ++i) {
        input.components[i].t = {static_cast<float>(i + 1), 0, 0};
        if (i) input.locals[i] = ThraxTrackingKernel::Relative(input.components[i], input.components[i - 1]);
    }
    Abi::Array<BoneTransform> output;
    alignas(16) std::array<BoneTransform, 7> storage{};
    Quaternion last = Identity;
    float consumed = 0;
    auto evaluate = [&](const Snapshot& copied, float delta, Quaternion previous, Quaternion& next, std::uint64_t) {
        input.frame = {copied.Component.Rotation, copied.Component.Translation, copied.Component.Scale};
        input.target_world = copied.Target; input.previous = previous; input.delta = delta;
        const auto result = ThraxTrackingKernel::Solve(input);
        if (result.status != Math::Status::ok || !ThraxTracking::Finite(result.offset)) return false;
        Require(result.yaw == 75 && result.pitch > 26 && result.pitch < 27);
        // The full correction reaches the head; alpha=.25 remains untouched for
        // the stock outer blend. Applying alpha inside the solver fails this.
        Require(std::fabs(Math::dot(result.poses.back().q, result.offset)) > .9999f);
        std::array<BoneTransform, 7> bones{};
        for (size_t i = 0; i < 7; ++i) {
            bones[i].Index = 58 + static_cast<int>(i);
            bones[i].Value = {result.poses[i].q, result.poses[i].t, 0, result.poses[i].s, 0};
        }
        if (!PublishOutput(output, bones, [&](void*, size_t bytes, size_t alignment) -> void* {
            Require(bytes == sizeof(storage) && alignment == 16); return storage.data();
        })) return false;
        Require(copied.Alpha == .25f); consumed += delta; next = result.offset; last = next; return true;
    };
    Require(state.Update(token, snapshot));
    Require(state.Evaluate(state.Prepare(token, snapshot.Stamp).value(), evaluate));
    Require(output.Count == 7 && consumed == .5f && std::fabs(last[2]) > .5f);
    const auto first = last;
    output.Count = 0; Require(state.Evaluate(token, evaluate));
    Require(consumed == .5f && last == first && output.Count == 7);
    // The current-pose data is validated before output publication.
    output.Count = 0; input.components[2].t[0] = std::numeric_limits<float>::quiet_NaN();
    Require(!state.Evaluate(token, evaluate) && output.Count == 0);
    input.components[2].t[0] = 3;
    Require(state.Update(token, snapshot));
    const auto stale = state.Prepare(token, snapshot.Stamp).value();
    ++snapshot.Stamp.Frame; ++snapshot.Stamp.Counter; Require(state.Update(token, snapshot));
    Require(!state.Evaluate(stale, evaluate) && output.Count == 0);
    Require(state.Evaluate(token, evaluate) && output.Count == 7);
    // Signed, nonuniform parent scale goes through the qualified relative path.
    Math::Transform parent, child;
    parent.s = {-2, 3, 4}; parent.t = {4, 5, 6}; child.s = {-4, 6, 8}; child.t = {2, 8, 10};
    const auto local = ThraxTrackingKernel::Relative(child, parent);
    Require(Math::validate(local) == Math::Status::ok);
    const auto reconstructed = Math::compose(parent, local);
    for (size_t i = 0; i < 3; ++i) Require(std::fabs(reconstructed.t[i] - child.t[i]) < 1.e-4f);
    std::cout << "thrax tracking kernel/state/output integration passed\n";
}
