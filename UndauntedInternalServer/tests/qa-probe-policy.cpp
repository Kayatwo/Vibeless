#include "../QaProbePolicy.h"
#include <cassert>
#include <string>
using namespace QaProbePolicy;

static std::optional<Request> parse(const std::string& text) { std::string error; return Parse(text, error); }
static std::string errorOf(const std::string& text) { std::string error; assert(!Parse(text, error)); return error; }

int main() {
    // Only launcher profiles named qa-* run the probe.
    assert(IsQaProfile(L"C:/Dauntless/Vibeless/profiles/qa-probe/"));
    assert(IsQaProfile(L"C:\\Dauntless\\Vibeless\\profiles\\qa-thunderdeep"));
    assert(!IsQaProfile(L"C:/Dauntless/Vibeless/profiles/larena/"));
    assert(!IsQaProfile(L"C:/profiles/qa-/"));
    assert(!IsQaProfile(L"C:/profiles/QA-probe/"));
    assert(!IsQaProfile(L""));

    assert(ValidClassPath("/Game/Monsters/skarn144/sq_ice_rock_bp.sq_ice_rock_bp_C"));
    assert(!ValidClassPath("/Game/Monsters/skarn144/sq_ice_rock_bp.sq_ice_rock_bp"));
    assert(ValidObjectPath("/Game/Monsters/skarn144/animation/sq_rock_animbp.sq_rock_animbp"));
    assert(!ValidObjectPath("/Script/Engine.Actor"));
    assert(!ValidObjectPath("/Game/../Engine/x.x"));
    assert(!ValidObjectPath("/Game/a b/c.c"));
    assert(!ValidObjectPath("/Game/Monsters/skarn144"));
    assert(!ValidObjectPath("/Game/Monsters.x/skarn144"));

    const auto request = parse(
        "# written by vibeless probe\r\nVersion=1\nId=skarn-0-2-1\n"
        "Creature=/Game/Monsters/skarn144/sq_ice_rock_bp.sq_ice_rock_bp_C\n"
        "Control = /Game/Monsters/sq/rock/sq_rock_normal_bp.sq_rock_normal_bp_C\nSampleSeconds=30\n"
        "Asset=/Game/Monsters/skarn144/a.a\nAsset=/Game/Monsters/skarn144/b.b\n");
    assert(request && request->Id == "skarn-0-2-1" && request->SampleSeconds == 30);
    assert(request->Control == "/Game/Monsters/sq/rock/sq_rock_normal_bp.sq_rock_normal_bp_C");
    assert(request->Assets.size() == 2 && request->Assets[1] == "/Game/Monsters/skarn144/b.b");
    const auto minimal = parse("Version=1\nId=a\nCreature=/Game/M/x.x_C");
    assert(minimal && minimal->SampleSeconds == 20 && minimal->Control.empty() && minimal->Assets.empty());

    assert(errorOf("Id=a\nCreature=/Game/M/x.x_C") == "Version, Id and Creature are required");
    assert(errorOf("Version=2\nId=a\nCreature=/Game/M/x.x_C").starts_with("unsupported request version"));
    assert(errorOf("Version=1\nId=A\nCreature=/Game/M/x.x_C") == "invalid Id");
    assert(errorOf("Version=1\nId=a\nId=b\nCreature=/Game/M/x.x_C") == "Id is given twice");
    assert(errorOf("Version=1\nId=a\nCreature=/Game/M/x.x_C\nSampleSeconds=1") == "SampleSeconds must be 2-120");
    assert(errorOf("Version=1\nId=a\nCreature=/Game/M/x.x_C\nSampleSeconds=-5") == "invalid SampleSeconds");
    assert(errorOf("Version=1\nId=a\nCreature=/Game/M/x.x_C\nAsset=/Game/a.a\nAsset=/Game/a.a").starts_with("Asset is listed twice"));
    assert(errorOf("Version=1\nId=a\nCreature=/Game/M/x.x_C\nControl=/Game/M/x.x_C") == "Control must differ from Creature");
    assert(errorOf("Version=1\nId=a\nCreature=/Game/M/x.x_C\nPort=1") == "unknown key Port");
    assert(errorOf("Version=1\nId=a\nCreature=/Game/M/x.x_C\njunk").starts_with("line without '='"));
    std::string many = "Version=1\nId=a\nCreature=/Game/M/x.x_C\n";
    for (int i = 0; i <= MaxAssets; ++i) many += "Asset=/Game/M/a" + std::to_string(i) + ".a\n";
    assert(errorOf(many) == "more than 4096 assets");

    // Motion: a still creature is frozen only when the control moved in the same server.
    MotionTrack still, moving, idle;
    for (int t = 0; t < 8; ++t) {
        assert(still.Add({{0, 0, 100}, {10, 0, 120}}));
        assert(moving.Add({{0, 0, 100}, {10, 0, 120 + t * 0.4}}));
        assert(idle.Add({{0, 0, 100.1}, {10, 0, 120}}));
    }
    assert(still.Samples() == 8 && still.Bones() == 2 && still.MovingBones() == 0);
    assert(moving.MovingBones() == 1 && moving.MaxDeltaCm() > 2.7 && moving.MaxDeltaCm() < 2.9);
    assert(idle.MovingBones() == 0 && idle.MaxDeltaCm() < StillCm);
    assert(Judge(moving, nullptr) == Motion::Moving);
    assert(Judge(still, &moving) == Motion::Frozen);
    assert(Judge(still, &idle) == Motion::Inconclusive);
    assert(Judge(still, nullptr) == Motion::Inconclusive);
    MotionTrack empty;
    assert(Judge(empty, &moving) == Motion::NoSamples);
    assert(!moving.Add({{0, 0, 0}}));                             // bone count must not change
    assert(!moving.Add({{0, 0, std::nan("")}, {0, 0, 0}}) && moving.Invalid() == 1);
    assert(!still.Add({}));

    // Report: first failing stage decides; strings are escaped; non-finite numbers are null.
    Report report;
    report.Id = "x"; report.Creature = "/Game/M/x.x_C";
    auto& load = report.Begin("load");
    report.Fail(load, "1 asset did not load");
    auto& spawn = report.Begin("spawn");
    report.Fail(spawn, "no navigable point");
    assert(report.Verdict == "fail-load" && report.Reason == "1 asset did not load");
    Subject creature; creature.Role = "creature"; creature.Track = still; creature.Health = std::nan("");
    creature.AddMontage("/Game/a.a"); creature.AddMontage("/Game/a.a"); creature.AddMontage("");
    assert(creature.Montages.size() == 1);
    Subject control; control.Role = "control"; control.Track = moving;
    report.Subjects = {creature, control};
    report.FailedAssets = {"/Game/q\"uote.q"};
    report.AssetsAsClass = 3;
    const auto json = report.ToJson();
    assert(json.find("\"asClass\": 3") != std::string::npos);
    assert(json.find("\"verdict\": \"fail-load\"") != std::string::npos);
    assert(json.find("\"verdict\": \"frozen-pose\"") != std::string::npos);
    assert(json.find("\"verdict\": \"moving\"") != std::string::npos);
    assert(json.find("\"health\": null") != std::string::npos);
    assert(json.find("q\\\"uote") != std::string::npos);
    assert(Quote("a\nb") == "\"a\\u000ab\"");
    assert(Number(1.23456) == "1.235");
    return 0;
}
