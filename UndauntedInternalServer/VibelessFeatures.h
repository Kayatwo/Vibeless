// Windows side of the feature host (FeatureHost.h): the process-wide host, core hook installation,
// the native features compiled into this runtime, and the startup report. AGPL-3.0-only.
//
// Native features (IDs used in DR_FEATURES and as package capabilities "native-feature:<id>"):
//   hunt-services   shared creature/hunt services: return travel, authored strength, menu recommendations
//   deepfrost       Deepfrost Gnasher pursuit, encounter, portrait and loot compatibility
//   thunderdeep     Thunderdeep Drask pursuit and portrait
//   test-arena      Test Arena destination controls (F8 panel, allowlisted spawn/despawn)
//   reward-probes   read-only reward diagnostics (server only, off unless listed; D10)
// A core-only build (VIBELESS_FEATURES=0) registers none of them.
#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <windows.h>
#include "FeatureHost.h"
#include "MinHook/MinHook.h"
#include "ProfileRoot.h"
#include "VibelessVersion.h"

namespace Vibeless {
inline FeatureHost::Host& Features() {
    static FeatureHost::Host host;
    return host;
}

// Claims base+rva for `owner`, then creates and enables the hook. A second owner is refused.
inline bool Hook(uintptr_t base, uintptr_t rva, void* detour, void** original, const char* name, const char* owner = "core") {
    auto& host = Features();
    const uintptr_t target = base + rva;
    if (const auto error = host.Claim(target, name, owner); !error.empty()) {
        host.AddError(error);
        std::cout << "Vibeless: " << error << std::endl;
        return false;
    }
    auto status = MH_CreateHook(reinterpret_cast<void*>(target), detour, original);
    if (status == MH_OK) status = MH_EnableHook(reinterpret_cast<void*>(target));
    host.MarkHook(target, status == MH_OK, status == MH_OK ? std::string() : "MinHook status " + std::to_string(status));
    return status == MH_OK;
}

inline FeatureHost::EnableList EnableListFromEnvironment() {
    char buffer[1024] = {};
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetEnvironmentVariableA("DR_FEATURES", buffer, sizeof buffer);
    if (length == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return FeatureHost::ParseEnableList(std::nullopt);
    if (length >= sizeof buffer) {
        Features().AddError("DR_FEATURES is longer than 1023 characters; no features enabled");
        return FeatureHost::ParseEnableList(std::string_view("none"));
    }
    return FeatureHost::ParseEnableList(std::string_view(buffer, length));
}

// <profile>/reports/runtime-client.json or runtime-server-<port>.json; nothing outside a profile.
inline void WriteReport(FeatureHost::Side side, int port) {
    const auto& root = ProfileRoot::Root();
    const auto report = Features().Report(VIBELESS_VERSION, port);
    for (const auto& state : Features().States())
        if (state.Enabled) std::cout << "Vibeless feature " << state.Id << " " << state.Version << " enabled" << std::endl;
    for (const auto& error : Features().Errors()) std::cout << "Vibeless: " << error << std::endl;
    if (root.empty()) return;
    std::error_code ec;
    const auto directory = std::filesystem::path(root) / L"reports";
    std::filesystem::create_directories(directory, ec);
    const auto name = side == FeatureHost::Side::Client ? std::wstring(L"runtime-client.json")
        : L"runtime-server-" + std::to_wstring(port) + L".json";
    const auto destination = directory / name;
    const auto temporary = directory / (name + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    { std::ofstream out(temporary, std::ios::binary | std::ios::trunc); out << report; }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING)) std::filesystem::remove(temporary, ec);
}
}

#if VIBELESS_FEATURES
namespace Vibeless {
inline void RegisterFeatures(uintptr_t base) {
    using FeatureHost::Event;
    using FeatureHost::Feature;
    using FeatureHost::Side;
    using FeatureHost::Tick;
    auto& host = Features();
    const auto add = [&host](FeatureHost::Feature feature) {
        if (const auto error = host.Register(std::move(feature)); !error.empty()) host.AddError(error);
    };
    const auto world = [](const Tick& t) { return static_cast<UWorld*>(t.World); };
    const auto driver = [](const Tick& t) { return static_cast<UNetDriver*>(t.Driver); };
    const auto object = [](const Event& e) { return static_cast<UObject*>(e.Object); };
    const auto function = [](const Event& e) { return static_cast<UFunction*>(e.Function); };

    Feature services;
    services.Id = "hunt-services";
    services.Version = "1.0.0";
    services.Install = [base](Side side) {
        if (side != Side::Client) return true;
        // FrostReturn installs its own three detours after checking their prologues; claim them first.
        static constexpr struct { uintptr_t Rva; const char* Name; } returnHooks[] = {
            {0x18DF100, "ReturnToRamsgate.Initialize"},
            {0x18F08A0, "ReturnToRamsgate.Confirmed"},
            {0x1599110, "Matchmaking.Cancel"}};
        auto& host = Features();
        for (const auto& hook : returnHooks)
            if (const auto error = host.Claim(base + hook.Rva, hook.Name, "hunt-services"); !error.empty()) {
                host.AddError(error);
                return false;
            }
        const bool installed = FrostReturn::Install(base);
        for (const auto& hook : returnHooks)
            host.MarkHook(base + hook.Rva, installed, installed ? std::string() : "not installed; see reports/frost-return.jsonl");
        // Return travel is degraded, not the whole feature: strength and recommendations still apply.
        if (!installed) host.AddError("hunt-services: return-to-Ramsgate hooks not installed");
        return true;
    };
    services.ClientTickBefore = [world](const Tick& t) { FrostReturn::Tick(world(t)); };
    services.ClientTickAfter = [world](const Tick& t) { FrostReturn::Tick(world(t)); HuntPresentation::Tick(world(t)); };
    services.ClientAfter = [object, function](const Event& e) { HuntPresentation::AfterEvent(object(e), function(e), e.Parms); };
    services.ServerTick = [world, driver](const Tick& t) { OriginalStrength::Tick(world(t), driver(t), t.Port); };
    add(std::move(services));

    Feature deepfrost;
    deepfrost.Id = "deepfrost";
    deepfrost.Version = "1.0.0";
    deepfrost.Requires = {"hunt-services"};
    deepfrost.ClientTickAfter = [world](const Tick& t) { FrostEncounter::TickClient(world(t)); FrostHunt::Tick(world(t)); };
    deepfrost.ServerTick = [world, driver](const Tick& t) {
        FrostEncounter::TickServer(world(t), driver(t), t.Port);
        FrostLootCompat::TickServer(world(t), driver(t), t.Port);
        FrostHunt::Tick(world(t));
    };
    add(std::move(deepfrost));

    Feature thunderdeep;
    thunderdeep.Id = "thunderdeep";
    thunderdeep.Version = "1.0.0";
    thunderdeep.Requires = {"hunt-services"};
    thunderdeep.ClientTickAfter = [world](const Tick& t) { DraskIcon::Tick(world(t)); ThunderdeepHunt::Tick(world(t)); };
    thunderdeep.ServerTick = [world](const Tick& t) { ThunderdeepHunt::Tick(world(t)); };
    add(std::move(thunderdeep));

    Feature arena;
    arena.Id = "test-arena";
    arena.Version = "1.0.0";
    arena.Requires = {"hunt-services"};
    arena.ClientTickAfter = [world](const Tick& t) { TestArenaMenu::Tick(world(t)); };
    arena.ClientIntercept = [object, function](const Event& e) { return TestArenaMenu::BeforeEvent(object(e), function(e), e.Parms); };
    arena.ServerIntercept = [object, function](const Event& e) {
        return TestArena::BeforeServerEvent(object(e), function(e), e.Parms, Networking::NetDriver);
    };
    add(std::move(arena));

    Feature probes;
    probes.Id = "reward-probes";
    probes.Version = "1.0.0";
    probes.Sides = Side::Server;
    probes.DefaultEnabled = false;
    probes.Install = [](Side) { FrostRewardProbe::Install(); return true; };
    probes.ServerTick = [world, driver](const Tick& t) {
        FrostRewardProbe::TickServer(world(t), driver(t), t.Port);
        DraskRewardProbe::TickServer(world(t), driver(t), t.Port);
    };
    probes.ServerBeforeOriginal = [object, function](const Event& e) {
        FrostRewardProbe::ObserveProcessEvent(object(e), function(e), e.Parms, false);
        DraskRewardProbe::ObserveProcessEvent(object(e), function(e), e.Parms, false);
    };
    probes.ServerAfter = [object, function](const Event& e) {
        FrostRewardProbe::ObserveProcessEvent(object(e), function(e), e.Parms, true);
        DraskRewardProbe::ObserveProcessEvent(object(e), function(e), e.Parms, true);
    };
    add(std::move(probes));
}
}
#else
namespace Vibeless { inline void RegisterFeatures(uintptr_t) {} }
#endif

namespace Vibeless {
// Registers and starts this side's features. Call once, after MH_Initialize and before core hooks.
inline void StartFeatures(uintptr_t base, FeatureHost::Side side) {
    RegisterFeatures(base);
    Features().Start(side, !ProfileRoot::Root().empty(), EnableListFromEnvironment());
}
}
