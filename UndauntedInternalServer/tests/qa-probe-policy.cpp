#include "../QaProbePolicy.h"
#include <cassert>
#include <limits>
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
    assert(json.find("\"side\": \"server\"") != std::string::npos && json.find("\"stream\"") == std::string::npos);
    assert(json.find("\"actions\"") == std::string::npos);

    // Server actions: every action-table row must play its montage on the live creature.
    ActionResult slam{"ShellSlam", "/Game/M/slam.slam_C", "/Game/M/slam_mot.slam_mot", true, 3.2, 3.5, 4, 1};
    ActionResult bomb{"FrostBomb", "/Game/M/bomb.bomb_C", "/Game/M/bomb_mot.bomb_mot", false, 0, 0, 0, 0};
    ActionResult none{"Taunt", "/Game/M/taunt.taunt_C", "", false, 0, 0, 0, 0};
    assert(JudgeActions({slam}).empty() && JudgeActions({}).empty());
    assert(JudgeActions({slam, bomb, none}) == "2 of 3 actions did not play, first FrostBomb (/Game/M/bomb_mot.bomb_mot)");
    assert(JudgeActions({none}) == "1 of 1 actions did not play, first Taunt (no montage)");
    // No table montage: both live action-class routes must really start, be observed, and finish.
    // Fixtures model the positive protocol; they do not assert a stock barrel-charge runtime pass.
    ActionResult dynamic = none;
    dynamic.Route = "action-class"; dynamic.Played = dynamic.Observed = dynamic.Completed = true;
    dynamic.SelectedMontage = "/Game/M/dynamic.dynamic";
    ActionResult stockDynamic = dynamic; stockDynamic.Role = "control";
    assert(JudgeActions({dynamic, stockDynamic}).empty());
    assert(!JudgeActions({dynamic}).empty() && !JudgeActions({stockDynamic}).empty());
    assert(!JudgeActions({dynamic, stockDynamic, stockDynamic}).empty());
    for (int failure = 0; failure < 5; ++failure) {
        auto bad = dynamic;
        if (failure == 0) bad.Played = false;
        if (failure == 1) bad.Observed = false;
        if (failure == 2) bad.Completed = false;
        if (failure == 3) bad.SelectedMontage.clear();
        if (failure == 4) bad.Error = "compound behavior-tree route unresolved";
        assert(!JudgeActions({bad, stockDynamic}).empty());
        auto badControl = bad; badControl.Role = "control";
        assert(!JudgeActions({dynamic, badControl}).empty());
        assert(!JudgeActions({bad, badControl}).empty());
    }
    auto wrongControl = stockDynamic; wrongControl.Action += "_different";
    assert(!JudgeActions({dynamic, wrongControl}).empty());
    // Parts and kill are judged against the stock control under the same forced damage.
    PartResult horn{"creature", "Horn", 2, 0, 0, true, false, false, 900, 900, {"Rock_Horn_1", "Rock_Horn_2"}};
    PartResult hornBroke = horn; hornBroke.After = 1;
    PartResult stockHorn{"control", "Horn", 2, 0, 1, true, false, false, 0, 900, {}};
    PartResult stockStill = stockHorn; stockStill.After = 0;
    PartResult tail{"creature", "Tail", 1, 0, 0, true, false, true, 0, 400, {}};
    assert(!horn.Broke() && hornBroke.Broke() && tail.Broke());
    assert(JudgeParts({horn, stockHorn}) == "no creature part broke under forced damage (control: 1 broke)");
    assert(JudgeParts({hornBroke, stockHorn}).empty() && JudgeParts({horn, tail, stockHorn}).empty());
    assert(JudgeParts({horn, stockStill}).empty());  // the method did not break stock parts either
    assert(JudgeParts({stockHorn}) == "the creature has no monster parts (control: 1)");
    KillResult dead{"creature", "rock_frost", {"OnDeath"}, true, 32000, 0, 1.5}, alive{"creature", "rock_frost", {}, false, 32000, 32000, 8};
    KillResult stockDead{"control", "rock_normal", {}, true, 32000, 0, 1.2};
    assert(JudgeKill({dead, stockDead}).empty() && JudgeKill({alive}).empty());
    assert(JudgeKill({alive, stockDead}) == "the creature did not die from lethal core damage (the control did)");
    Report acted;
    acted.Parts = {hornBroke}; acted.Kills = {dead};
    assert(acted.ToJson().find("\"loot\": [\"Rock_Horn_1\", \"Rock_Horn_2\"]") != std::string::npos);
    assert(acted.ToJson().find("\"metadataKey\": \"rock_frost\", \"died\": true") != std::string::npos);
    assert(acted.ToJson().find("\"deathEvents\": [\"OnDeath\"]") != std::string::npos);
    acted.Actions = {slam};
    assert(acted.ToJson().find("\"row\": \"ShellSlam\"") != std::string::npos && acted.ToJson().find("\"notifies\": 4, \"spawned\": 1") != std::string::npos);
    acted.Actions = {dynamic, stockDynamic};
    assert(acted.ToJson().find("\"route\": \"action-class\"") != std::string::npos);
    assert(acted.ToJson().find("\"role\": \"control\"") != std::string::npos);
    assert(acted.ToJson().find("\"observed\": true, \"completed\": true") != std::string::npos);
    assert(acted.ToJson().find("\"spawnedClassesDropped\": 0, \"spawnedClasses\": []") != std::string::npos);

    // Spawn census: one entry per class with counts and bounded ancestry; overflow is counted, never
    // silently truncated, and an empty class name is a drop rather than a fabricated entry.
    ActionResult clone = slam;
    const std::string stockClone = "/Game/Monsters/moonface/crowbear_clone_bp.crowbear_clone_bp_C";
    RecordSpawn(clone, "/Game/Monsters/bloodshotshrowd144/crowbear_alpha_clone_bp.crowbear_alpha_clone_bp_C",
        {stockClone, "/Game/Monsters/moonface/crowbear_archetype_bp.crowbear_archetype_bp_C"});
    RecordSpawn(clone, "/Game/Monsters/bloodshotshrowd144/crowbear_alpha_clone_bp.crowbear_alpha_clone_bp_C", {});
    assert(clone.SpawnedClasses.size() == 1 && clone.SpawnedClasses[0].Count == 2 && clone.SpawnedClasses[0].Ancestors[0] == stockClone);
    RecordSpawn(clone, "", {});
    assert(clone.SpawnedClassesDropped == 1);
    RecordSpawn(clone, "/Game/X/long.long_C", std::vector<std::string>(MaxSpawnAncestors + 3, "/Game/X/a.a_C"));
    assert(clone.SpawnedClasses.back().Ancestors.size() == MaxSpawnAncestors);
    for (size_t i = 0; i < MaxSpawnedClasses + 2; ++i) RecordSpawn(clone, "/Game/X/c" + std::to_string(i) + ".c_C", {});
    assert(clone.SpawnedClasses.size() == MaxSpawnedClasses && clone.SpawnedClassesDropped == 1 + 4);
    acted.Actions = {clone};
    assert(acted.ToJson().find("{\"class\": \"/Game/Monsters/bloodshotshrowd144/crowbear_alpha_clone_bp.crowbear_alpha_clone_bp_C\", \"count\": 2, \"ancestors\": [\""
        + stockClone + "\", ") != std::string::npos);
    assert(acted.ToJson().find("\"spawnedClassesDropped\": 5") != std::string::npos);

    // Diagnostic state must distinguish unavailable from unknown, stay bounded and never waive
    // a failed action (including the stock-negative case).
    ActionTrace trace;
    trace.Role = "control"; trace.Point = "before-stop-ai"; trace.Row = "barrelchg";
    trace.BaseValue = 0; trace.Available = false; trace.OverrideCount = 0;
    trace.Health = 21000; trace.MaxHealth = 28000; trace.RequestedDamage = 7000;
    trace.Enraged = false; trace.AetherCharged = true; trace.ComponentCount = 1;
    trace.Components = {"actor:health_rules=stock.action_swapper_bpc_C"};
    acted.Trace(trace);
    auto traced = acted.ToJson();
    assert(traced.find("\"baseValue\": 0.000, \"available\": false") != std::string::npos);
    assert(traced.find("\"overrideCount\": 0") != std::string::npos);
    assert(traced.find("\"missingEventsInconclusive\": true") != std::string::npos);
    assert(traced.find("\"health\": 21000.000, \"maxHealth\": 28000.000, \"requestedDamage\": 7000.000") != std::string::npos);
    assert(traced.find("\"enraged\": false, \"aetherCharged\": true, \"componentCount\": 1") != std::string::npos);
    assert(traced.find("actor:health_rules=stock.action_swapper_bpc_C") != std::string::npos);
    trace.BaseValue.reset(); trace.Available.reset(); trace.OverrideCount.reset();
    trace.Error = "signature unavailable";
    acted.Trace(trace);
    assert(acted.ToJson().find("\"baseValue\": null, \"available\": null") != std::string::npos);
    for (size_t i = acted.ActionTraces.size(); i < MaxActionTrace + 3; ++i) acted.Trace(trace);
    assert(acted.ActionTraces.size() == MaxActionTrace && acted.ActionTracesDropped == 3);
    acted.Actions = {none};
    assert(!JudgeActions(acted.Actions).empty());
    assert(ActionTraceDamage(28000, 28000, 0) == 7000);
    assert(ActionTraceDamage(21000, 28000, 1) == 7000);
    assert(ActionTraceDamage(14000, 28000, 2) == 7000);
    assert(ActionTraceDamage(2000, 28000, 2) == 0); // never heal a subject already below target
    assert(ActionTraceDamage(28000, 28000, 2) == 14000); // one call is bounded to half current health
    assert(!ActionTraceDamage(0, 28000, 0) && !ActionTraceDamage(1, 0, 0));
    assert(!ActionTraceDamage(28000, 28000, 3) && !ActionTraceDamage(28000, 28000, -1));
    assert(!ActionTraceDamage(30000, 28000, 0));
    assert(!ActionTraceDamage(std::numeric_limits<double>::infinity(), 28000, 0));
    assert(!ActionTraceDamage(28000, std::numeric_limits<double>::quiet_NaN(), 0));

    // Client streaming: a full chain down to 1x1, less the combined LOD bias, at least one mip.
    assert(FullChainMips(2048, 2048, 0) == 12 && FullChainMips(2048, 512, 1) == 11);
    assert(FullChainMips(1, 1, 0) == 1 && FullChainMips(4, 4, 9) == 1 && FullChainMips(0, 0, 0) == 0 && FullChainMips(0, 64, 0) == 7);
    TextureState full{"/Game/t/a.a", 1024, 1024, 0, 7, 11, true, false, false};
    TextureState shy{"/Game/t/b.b", 1024, 1024, 0, 7, 9, true, false, false};
    TextureState stuck{"/Game/t/c.c", 512, 512, 0, 6, 6, true, false, true};
    TextureState inline_{"/Game/t/d.d", 64, 64, 0, 7, 3, false, false, false};
    assert(!full.Short() && shy.Short() && !stuck.Short() && !inline_.Short());
    const auto summary = Summarize({full, shy, stuck, inline_});
    assert(summary.Textures == 4 && summary.Streamable == 3 && summary.Pending == 1 && summary.Short == 1 && summary.Raised == 2);
    StreamSettle settle;
    assert(!settle.Observe(10, 0, 0.0));     // first observation starts the clock
    assert(!settle.Observe(10, 0, 2.0));
    assert(!settle.Observe(12, 0, 2.5));     // resident mips changed: restart
    assert(!settle.Observe(12, 1, 6.0));     // an update is pending: restart
    assert(!settle.Observe(12, 0, 8.0));
    assert(settle.Observe(12, 0, 11.0));

    // Client portrait: an empty creature portrait is missing only when the control showed one.
    Portrait shown{true, true, true, "/Game/UI/x.x", "hud", 512, 512}, blank{true, true, false, "", "", 0, 0}, unbound{};
    assert(Judge(shown, nullptr) == PortraitResult::Shown);
    assert(Judge(blank, &shown) == PortraitResult::Missing);
    assert(Judge(blank, &blank) == PortraitResult::Inconclusive && Judge(blank, nullptr) == PortraitResult::Inconclusive);
    assert(Judge(unbound, &shown) == PortraitResult::Missing);
    Report client;
    client.Side = "client"; client.Frames = 120;
    Subject seen; seen.Role = "creature"; seen.Hud = blank; seen.Key = "rock_frost";
    Subject stock; stock.Role = "control"; stock.Hud = shown;
    client.Subjects = {seen, stock};
    client.Textures = {full, shy};
    const auto clientJson = client.ToJson();
    assert(clientJson.find("\"side\": \"client\"") != std::string::npos && clientJson.find("\"frames\": 120") != std::string::npos);
    assert(clientJson.find("\"portrait\": {\"verdict\": \"missing\"") != std::string::npos);
    assert(clientJson.find("\"portrait\": {\"verdict\": \"shown\"") != std::string::npos);
    assert(clientJson.find("\"key\": \"rock_frost\"") != std::string::npos && clientJson.find("\"source\": \"hud\"") != std::string::npos);
    assert(clientJson.find("\"short\": 1") != std::string::npos && clientJson.find("\"expected\": 11") != std::string::npos);
    return 0;
}
