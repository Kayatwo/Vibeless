// Feature host rules: enable list, dependencies, side filtering, dispatch order, hook ownership
// and the startup report. AGPL-3.0-only.
#include "../FeatureHost.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace FeatureHost;

static std::vector<std::string> Trace;

static Feature Make(std::string id, std::vector<std::string> needs = {}, Side sides = Side::Both, bool byDefault = true) {
    Feature f;
    f.Id = id;
    f.Version = "1.0.0";
    f.Sides = sides;
    f.DefaultEnabled = byDefault;
    f.Requires = std::move(needs);
    f.ServerTick = [id](const Tick&) { Trace.push_back(id + ":tick"); };
    f.ClientTickBefore = [id](const Tick&) { Trace.push_back(id + ":before"); };
    f.ServerIntercept = [id](const Event& e) { Trace.push_back(id + ":intercept"); return e.Parms == &Trace && id == "test-arena"; };
    return f;
}

static Host Standard() {
    Host host;
    auto services = Make("hunt-services");
    services.Internal = true;
    assert(host.Register(services).empty());
    assert(host.Register(Make("deepfrost", {"hunt-services"})).empty());
    assert(host.Register(Make("thunderdeep", {"hunt-services"})).empty());
    assert(host.Register(Make("test-arena", {"hunt-services"})).empty());
    assert(host.Register(Make("reward-probes", {}, Side::Server, false)).empty());
    return host;
}

static bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

int main() {
    // Enable-list parsing.
    assert(ParseEnableList(std::nullopt).Defaults);
    assert(!ParseEnableList(std::string_view("none")).Defaults && ParseEnableList(std::string_view("none")).Ids.empty());
    assert(ParseEnableList(std::string_view("  ")).Ids.empty() && !ParseEnableList(std::string_view("  ")).Defaults);
    const auto list = ParseEnableList(std::string_view(" a, b  c,,a "));
    assert(!list.Defaults && list.Ids == std::set<std::string>({"a", "b", "c"}));

    // Registration rules.
    {
        Host host;
        assert(host.Register(Make("a")).empty());
        assert(Contains(host.Register(Make("a")), "registered twice"));
        assert(Contains(host.Register(Make("b", {"c"})), "requires c, which is not registered before it"));
        assert(Contains(host.Register(Make("")), "without an ID"));
    }

    // Defaults: everything except default-off features; reward probes stay off.
    {
        auto host = Standard();
        host.Start(Side::Server, true, ParseEnableList(std::nullopt));
        assert(host.Enabled("deepfrost") && host.Enabled("test-arena"));
        assert(!host.Enabled("reward-probes"));
        assert(host.States()[4].Reason == "off by default");
        Trace.clear();
        host.ServerTick({});
        assert((Trace == std::vector<std::string>{"hunt-services:tick", "deepfrost:tick",
            "thunderdeep:tick", "test-arena:tick"}));
    }

    // The compatibility contract sees only the features packages name, not internal services.
    {
        auto host = Standard();
        host.Start(Side::Client, true, ParseEnableList(std::string_view("test-arena")));
        assert(host.Enabled("hunt-services"));
        assert((host.PackageFeatures() == std::set<std::string>{"test-arena"}));
    }

    // Core only: nothing subscribes, so the runtime installs no feature hooks.
    {
        auto host = Standard();
        host.Start(Side::Client, true, ParseEnableList(std::string_view("none")));
        assert(!host.HasClientTick());
        for (const auto& state : host.States()) assert(!state.Enabled && state.Reason == "not in DR_FEATURES");
        Trace.clear();
        host.ServerTick({});
        assert(!host.ServerIntercept({}));
        assert(Trace.empty());
    }

    // Outside a profile every feature is off, whatever the list says.
    {
        auto host = Standard();
        host.Start(Side::Client, false, ParseEnableList(std::nullopt));
        for (const auto& state : host.States()) assert(!state.Enabled && state.Reason == "not in a Vibeless profile");
        assert(!host.HasClientTick());
    }

    // A listed feature brings the features it requires; unknown IDs are reported.
    {
        auto host = Standard();
        host.Start(Side::Server, true, ParseEnableList(std::string_view("test-arena,typo.feature")));
        assert(host.Enabled("test-arena") && host.Enabled("hunt-services"));
        assert(host.States()[0].Reason == "required by test-arena" && host.States()[3].Reason.empty());
        assert(!host.Enabled("deepfrost") && host.States()[1].Reason == "not in DR_FEATURES");
        assert(host.Errors().size() == 1 && Contains(host.Errors()[0], "unknown feature typo.feature"));
    }

    // Arena alone with its services; intercept stops at the first feature that consumes the event.
    {
        auto host = Standard();
        host.Start(Side::Server, true, ParseEnableList(std::string_view("test-arena")));
        assert(host.Enabled("test-arena") && !host.Enabled("deepfrost"));
        Trace.clear();
        Event consumed;
        consumed.Parms = &Trace;
        assert(host.ServerIntercept(consumed));
        assert((Trace == std::vector<std::string>{"hunt-services:intercept", "test-arena:intercept"}));
    }

    // Side filtering: a server-only feature never runs in the client, even when listed.
    {
        auto host = Standard();
        host.Start(Side::Client, true, ParseEnableList(std::string_view("reward-probes")));
        assert(!host.Enabled("reward-probes") && host.States()[4].Reason == "not used on the client");
    }

    // A failed install keeps the feature off and its dependents too.
    {
        Host host;
        auto base = Make("hunt-services");
        base.Install = [](Side) { return false; };
        assert(host.Register(base).empty());
        assert(host.Register(Make("test-arena", {"hunt-services"})).empty());
        host.Start(Side::Client, true, ParseEnableList(std::nullopt));
        assert(host.States()[0].Reason == "install failed");
        assert(host.States()[1].Reason == "requires hunt-services, which is off");
        assert(!host.HasClientTick());
    }

    // Hook ownership: a second owner for the same address is refused with both names.
    {
        Host host;
        assert(host.Claim(0x3307100, "UGameEngine::Tick", "core").empty());
        const auto clash = host.Claim(0x3307100, "UGameEngine::Tick", "deepfrost");
        assert(Contains(clash, "owned by core") && Contains(clash, "deepfrost cannot also hook it"));
        assert(host.Claim(0x18DF100, "ReturnToRamsgate.Initialize", "hunt-services").empty());
        host.MarkHook(0x3307100, true);
        host.MarkHook(0x18DF100, false, "MH_CreateHook status 5");
        assert(host.Hooks().size() == 2 && host.Hooks()[0].Active && !host.Hooks()[1].Active);
    }

    // Report: valid structure, reasons and hook owners, escaped text.
    {
        auto host = Standard();
        host.Claim(1, "Engine \"tick\"", "core");
        host.MarkHook(1, true);
        host.Start(Side::Server, true, ParseEnableList(std::string_view("hunt-services")));
        const auto report = host.Report("0.1.0", 8761);
        assert(Contains(report, "\"api\": 1") && Contains(report, "\"side\": \"server\"") && Contains(report, "\"port\": 8761"));
        assert(Contains(report, "{\"id\": \"hunt-services\", \"version\": \"1.0.0\", \"enabled\": true}"));
        assert(Contains(report, "\"reason\": \"not in DR_FEATURES\""));
        assert(Contains(report, "\"name\": \"Engine \\\"tick\\\"\", \"owner\": \"core\", \"active\": true"));
        assert(Contains(report, "\"errors\": []") && Contains(report, "\"info\": {}"));
        host.SetInfo("contentRegistry", "C:/P/tables/native/registry.txt");
        host.SetInfo("contentEntries", "3");
        assert(Contains(host.Report("0.1.0", 0), "\"info\": {\"contentEntries\": \"3\", \"contentRegistry\": \"C:/P/tables/native/registry.txt\"}"));
        assert(Host::Quote(std::string("a\nb")) == "\"a\\u000ab\"");
    }

    std::cout << "feature host tests passed" << std::endl;
    return 0;
}
