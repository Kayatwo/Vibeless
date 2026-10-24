#include "../ThraxTrackingRuntimePolicy.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
using namespace ThraxTrackingRuntime;
static void Require(bool condition) { if (!condition) std::abort(); }
int main() {
    Require(EffectiveAlpha(0, 1, 1) == 0 && EffectiveAlpha(1, .5f, .5f) == .25f);
    Require(EffectiveAlpha(1, 2, 2) == 1 && EffectiveAlpha(1, -1, 1) == 0);
    Require(!EffectiveAlpha(1, std::numeric_limits<float>::infinity(), 0));
    Require(!EffectiveAlpha(1, std::numeric_limits<float>::max(), 2));
    std::array<std::int32_t, 7> indices{58, 59, 60, 61, 62, 63, 64}, parents{57, 58, 59, 60, 61, 62, 63};
    Require(OrderedChain(indices, parents)); parents[4] = 57; Require(!OrderedChain(indices, parents));
    std::array<BoneTransform, 7> values{};
    for (size_t i = 0; i < values.size(); ++i) { values[i].Index = indices[i]; values[i].Value.Rotation = Identity; values[i].Value.Scale = {1, 1, 1}; }
    alignas(16) std::array<BoneTransform, 7> engineStorage{};
    Abi::Array<BoneTransform> output;
    int allocations = 0;
    auto allocator = [&](void* old, size_t bytes, size_t alignment) -> void* {
        Require(!old && output.Count == 0 && bytes == 7 * 64 && alignment == 16); ++allocations; return engineStorage.data();
    };
    Require(PublishOutput(output, values, allocator) && output.Count == 7 && allocations == 1 && output.Data[6].Index == 64);
    Require(!PublishOutput(output, values, allocator)); output.Count = 0;
    Require(PublishOutput(output, values, allocator) && allocations == 1);
    output.Count = 0; values[2].Value.Scale[0] = std::numeric_limits<float>::quiet_NaN();
    Require(!PublishOutput(output, values, allocator) && output.Count == 0);
    values[2].Value.Scale[0] = 1; values[2].Value.Rotation = {};
    Require(!PublishOutput(output, values, allocator) && output.Count == 0);
    State state;
    const Owner first{1, 2, 3, 4, 1001}, second{5, 6, 7, 8, 1002};
    auto token = state.Initialize(first).value();
    auto other = state.Initialize(second).value();
    Snapshot input; input.Alpha = .5f; input.Delta = .125f; input.Target = {2, 3, 4}; input.Stamp = {123, 5};
    auto miss = [](const Snapshot&, float, Quaternion, Quaternion&, std::uint64_t) { Require(false); return false; };
    Require(!state.Evaluate(token, miss));
    Require(state.Update(token, input));
    Require(!state.Prepare(token, Traversal{122, 5})); Require(!state.Prepare(token, Traversal{123, 4}));
    Require(!state.Prepare(token, Traversal{})); Require(state.Prepare(token, Traversal{123, 5}).has_value());
    const auto stale = state.Prepare(token).value();
    Require(state.Update(token, input)); Require(!state.Evaluate(stale, miss));
    Require(!state.Reset(stale)); Require(state.Prepare(token, input.Stamp).has_value());
    Require(state.Update(other, input));
    input.Target[0] = 99;
    float time = 0;
    std::atomic<int> evaluations = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back([&] {
        Require(state.Evaluate(token, [&](const Snapshot& value, float delta, Quaternion previous, Quaternion& next, std::uint64_t epoch) {
            Require(value.Target[0] == 2 && value.Alpha == .5f && value.Delta == 0 && epoch == 2);
            Require(previous[0] == static_cast<float>(evaluations.load()));
            time += delta; next[0] = static_cast<float>(++evaluations); return true;
        }));
    });
    for (auto& thread : threads) thread.join();
    Require(time == .25f && evaluations == 8);
    // A frame gap alone is not source irrelevance: URO can legitimately skip.
    input.Target[0] = 2; input.Stamp = {200, 6}; input.Delta = 0;
    Require(state.Update(token, input));
    Require(state.Evaluate(token, [](const Snapshot&, float delta, Quaternion previous, Quaternion&, std::uint64_t) {
        Require(delta == 0 && previous[0] == 8); return true;
    }));
    input.Delta = .125f;
    Require(state.Evaluate(other, [](const Snapshot&, float delta, Quaternion previous, Quaternion&, std::uint64_t) {
        Require(delta == .125f && previous == Identity); return true;
    }));
    input.Alpha = 0; Require(state.Update(token, input)); Require(!state.Evaluate(token, miss));
    input.Alpha = 1; Require(state.Update(token, input));
    Require(state.Evaluate(token, [](const Snapshot&, float delta, Quaternion previous, Quaternion&, std::uint64_t) {
        Require(delta == .125f && previous == Identity); return true;
    }));
    // Failed solve/output commit clears history and time; no half-published state.
    Require(!state.Evaluate(token, [](const Snapshot&, float, Quaternion, Quaternion&, std::uint64_t) { return false; }));
    Require(!state.Evaluate(token, miss));
    input.Delta = std::numeric_limits<float>::infinity(); Require(!state.Update(token, input));
    input.Delta = -.01f; Require(!state.Update(token, input));
    input.Delta = .125f; input.Component.Translation[0] = std::numeric_limits<float>::quiet_NaN(); Require(!state.Update(token, input));
    input.Component.Translation[0] = 0;
    auto replacement = state.Initialize(first).value();
    Require(replacement.Generation != token.Generation && !state.Update(token, input));
    Require(!state.Evaluate(token, miss));
    auto recycled = first; ++recycled.Serial;
    Require(!state.Find(recycled)); Require(!state.Initialize({}));
    Require(state.Update(replacement, input)); state.End(first.Node);
    Require(!state.Find(first) && !state.Evaluate(replacement, miss));
    // Concurrent independent update/evaluation streams retain each owner's target.
    state.Initialize(first); token = state.Find(first).value();
    threads.clear();
    for (auto pair : {std::pair{token, 17.f}, std::pair{other, 42.f}}) threads.emplace_back([&, pair] {
        Snapshot value; value.Alpha = 1; value.Delta = .125f; value.Target[0] = pair.second; value.Stamp = {456, 7};
        for (int i = 0; i < 1000; ++i) {
            Require(state.Update(pair.first, value));
            Require(state.Evaluate(pair.first, [&](const Snapshot& snapshot, float delta, Quaternion, Quaternion&, std::uint64_t) {
                Require(snapshot.Target[0] == pair.second && delta == .125f); return true;
            }));
        }
    });
    for (auto& thread : threads) thread.join();
    std::cout << "thrax tracking runtime state passed\n";
}
