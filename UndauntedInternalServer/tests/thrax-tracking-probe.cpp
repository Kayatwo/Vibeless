// Real observer policy, layouts, budget and original-forwarding boundary. AGPL-3.0-only.
#include "../ThraxTrackingProbePolicy.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
using namespace ThraxTrackingProbePolicy;
int main() {
    assert(QaProfile(L"qa-probe", L"1"));
    for (const auto name : {L"playtest", L"larena", L"qa-thunderdeep", L"qa-other", L"QA-probe", L"qa-probe/../playtest"}) assert(!QaProfile(name, L"1"));
    assert(!QaProfile(L"qa-probe", L"") && !QaProfile(L"qa-probe", L"true"));
    int original = 0, observation = 0;
    std::array<std::byte, 256> originalBytes{};
    const auto before = originalBytes;
    AfterOriginal([&] { ++original; }, [&] { ++observation; assert(original == 1); });
    assert(original == 1 && observation == 1 && before == originalBytes);
    AfterOriginal([&] { ++original; }, [&] { ++observation; throw 1; });
    assert(original == 2 && observation == 2);
    // The observer cannot swallow an original failure or run after it failed.
    try { AfterOriginal([&] { ++original; throw 2; }, [&] { ++observation; }); assert(false); } catch (int error) { assert(error == 2); }
    assert(original == 3 && observation == 2);
    std::array<Transform, 8> pose{};
    std::array<std::uint8_t, 8> flags{};
    Context context; context.Pose = {pose.data(), 8, 8}; context.Flags = {flags.data(), 8, 8};
    BoneReference bone; bone.CompactIndex = 7;
    assert(ValidIndex(bone, context));
    bone.IndexMode = -1; assert(!ValidIndex(bone, context));
    bone.IndexMode = 0; bone.CompactIndex = -1; assert(!ValidIndex(bone, context));
    bone.CompactIndex = 8; assert(!ValidIndex(bone, context));
    bone.CompactIndex = 7; context.Flags.Count = 7; assert(!ValidIndex(bone, context));
    context.Flags.Count = 8; context.Pose.Data = nullptr; assert(!ValidIndex(bone, context));
    context.Pose.Data = pose.data(); context.Pose.Capacity = 7; assert(!ValidIndex(bone, context));
    context.Pose.Capacity = 65537; assert(!ValidIndex(bone, context));
    Transform transform; transform.Rotation[3] = 1; transform.Scale = {-1, 2, 3};
    assert(Finite(transform)); transform.Translation[0] = std::numeric_limits<float>::infinity(); assert(!Finite(transform));
    Queue queue;
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 8; ++worker) workers.emplace_back([&] {
        for (int attempt = 0; attempt < 20; ++attempt) {
            const auto ordinal = queue.Reserve();
            if (!ordinal) continue;
            Sample sample; sample.Ordinal = *ordinal; sample.Target[0] = static_cast<float>(*ordinal);
            queue.Publish(sample); sample.Target[0] = -1; queue.Publish(sample);
        }
    });
    for (auto& worker : workers) worker.join();
    assert(queue.Full());
    const auto batch = queue.Drain(); assert(batch.Count == SampleLimit);
    std::array<bool, SampleLimit> seen{};
    for (size_t index = 0; index < batch.Count; ++index) {
        const auto& sample = batch.Samples[index]; assert(sample.Ordinal > 0 && sample.Ordinal <= SampleLimit);
        assert(!seen[sample.Ordinal - 1]); seen[sample.Ordinal - 1] = true;
        assert(sample.Target[0] == static_cast<float>(sample.Ordinal));
    }
    assert(queue.Drain().Count == 0 && !queue.Reserve());
    Queue delayed;
    const auto ordinal = *delayed.Reserve(); assert(delayed.Drain().Count == 0);
    Sample sample; sample.Ordinal = ordinal; delayed.Publish(sample);
    assert(delayed.Drain().Count == 1); delayed.Publish(sample); assert(delayed.Drain().Count == 0);
    std::cout << "Thrax observation policy: scope, native layouts, index rejection, bounded queue and forwarding pass\n";
}
