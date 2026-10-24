// Exercise the production state boundary without an engine or SDK. AGPL-3.0-only.
#include "../ThraxTrackingState.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
using namespace ThraxTracking;

static Eligibility Allowed() { return {true, true, AnimationClass, Marker, NodeName}; }
static Input Sample(float delta = 0.25f) {
    Input value;
    value.Target = {2, 3, 4}; value.Component.Translation = {5, 6, 7};
    value.Component.Scale = {-1, 2, 3}; value.Alpha = 0.7f;
    value.DeltaSeconds = delta; value.Relevant = true;
    return value;
}
int main() {
    State state;
    const auto allowed = Allowed();
    for (int field = 0; field < 5; ++field) {
        auto denied = allowed;
        if (field == 0) denied.InProfile = false;
        if (field == 1) denied.FeatureEnabled = false;
        if (field == 2) denied.ClassPath = "/Game/Monsters/ml/randall/blueprints/ml_randall_anim_bp.ml_randall_anim_bp_C";
        if (field == 3) denied.PoseMarker = "Thrax144_B1_BasePose";
        if (field == 4) denied.ReflectedNode = "AnimGraphNode_ArchonSaveComponentBoneTransform";
        assert(!state.Initialize({1, 2}, denied));
    }
    assert(!state.Initialize({0, 2}, allowed));
    auto token = *state.Initialize({1, 2}, allowed);
    assert(!state.Consume(token, allowed, 1));
    assert(!state.Update(token, allowed, 0, Sample()));
    auto sample = Sample();
    assert(state.Update(token, allowed, 1, sample));
    sample.Target[0] = 99; sample.Component.Translation[1] = 42;
    assert(state.Update(token, allowed, 2, sample));
    sample.Target[0] = -99; // Publication owns a copy.
    assert(!state.Consume(token, allowed, 1));
    assert(!state.Update(token, allowed, 1, sample));
    auto evaluated = *state.Consume(token, allowed, 2);
    assert(evaluated.AccumulatedDelta == 0.5f && evaluated.Snapshot.Target[0] == 99);
    assert(evaluated.Snapshot.Component.Translation[1] == 42 && evaluated.Snapshot.Component.Scale[0] == -1);
    assert(evaluated.Snapshot.Alpha == 0.7f && evaluated.Snapshot.DeltaSeconds == 0);
    assert(state.Consume(token, allowed, 2)->AccumulatedDelta == 0);
    assert(state.Update(token, allowed, 3, Sample(0)));
    assert(state.Consume(token, allowed, 3)->AccumulatedDelta == 0);
    assert(state.Update(token, allowed, 4, Sample(-0.25f)));
    assert(state.Consume(token, allowed, 4)->AccumulatedDelta == -0.25f);
    auto irrelevant = Sample(); irrelevant.Relevant = false;
    assert(state.Update(token, allowed, 5, irrelevant));
    auto reset = *state.Consume(token, allowed, 5);
    assert(!reset.Snapshot.Relevant && reset.AccumulatedDelta == 0 && reset.ResetRevision > evaluated.ResetRevision);
    assert(state.Update(token, allowed, 6, Sample()));
    assert(state.Consume(token, allowed, 6)->AccumulatedDelta == 0.25f);
    std::uint64_t epoch = 7;
    for (int field = 0; field < 6; ++field) {
        auto invalid = Sample();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        if (field == 0) invalid.Target[1] = nan;
        if (field == 1) invalid.Component.Rotation[0] = nan;
        if (field == 2) invalid.Component.Translation[0] = nan;
        if (field == 3) invalid.Component.Scale[0] = nan;
        if (field == 4) invalid.Alpha = nan;
        if (field == 5) invalid.DeltaSeconds = std::numeric_limits<float>::infinity();
        assert(!state.Update(token, allowed, epoch, invalid));
        assert(!state.Consume(token, allowed, epoch++));
        assert(state.Update(token, allowed, epoch, Sample()));
        assert(state.Consume(token, allowed, epoch++)->AccumulatedDelta == 0.25f);
    }
    const float max = (std::numeric_limits<float>::max)();
    assert(state.Update(token, allowed, epoch++, Sample(max)));
    assert(!state.Update(token, allowed, epoch, Sample(max)));
    assert(!state.Consume(token, allowed, epoch++));
    assert(state.Update(token, allowed, epoch, Sample()));
    auto disabled = allowed; disabled.FeatureEnabled = false;
    assert(!state.Consume(token, disabled, epoch));
    assert(!state.Consume(token, allowed, epoch++));
    assert(state.Update(token, allowed, epoch++, Sample()));
    assert(state.Reset(token));
    assert(!state.Consume(token, allowed, epoch - 1));
    const auto replacement = *state.Initialize(token.Owner, allowed);
    assert(replacement.Generation != token.Generation);
    assert(!state.End(token) && !state.Reset(token) && !state.Update(token, allowed, epoch, Sample()));
    token = replacement;
    assert(state.Update(token, allowed, 1, Sample()));
    // Concurrent evaluations may consume the accumulated time only once.
    std::array<float, 8> deltas{};
    std::vector<std::thread> readers;
    for (size_t i = 0; i < deltas.size(); ++i) readers.emplace_back([&, i] { deltas[i] = state.Consume(token, allowed, 1)->AccumulatedDelta; });
    for (auto& reader : readers) reader.join();
    float total = 0; for (const float delta : deltas) total += delta;
    assert(total == 0.25f);
    // Distinct instances AND distinct nodes on one instance have independent state.
    std::array<Token, 3> tokens{token, *state.Initialize({3, 2}, allowed), *state.Initialize({1, 4}, allowed)};
    std::vector<std::thread> workers;
    for (size_t i = 0; i < tokens.size(); ++i) workers.emplace_back([&, i] {
        for (std::uint64_t step = 2; step < 1002; ++step) {
            auto input = Sample(); input.Target[0] = static_cast<float>(i);
            assert(state.Update(tokens[i], allowed, step, input));
            const auto output = *state.Consume(tokens[i], allowed, step);
            assert(output.Snapshot.Target[0] == static_cast<float>(i) && output.AccumulatedDelta == 0.25f);
        }
    });
    for (auto& worker : workers) worker.join();
    assert(state.End(token));
    assert(!state.Consume(token, allowed, 1001));
    assert(!state.Update(token, allowed, 1002, Sample()));
    std::cout << "Thrax tracking state: lifecycle, snapshots, eligibility and concurrency pass\n";
}
