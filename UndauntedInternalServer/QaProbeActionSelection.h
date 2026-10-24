// Actor-selected legacy action table. AGPL-3.0-only.
#pragma once
#include <string>

namespace QaProbeActionSelection {
// No inventory fallback: loading a diagnostic/other actor's table must never make it
// that subject's action table. Callbacks validate reflected property/class/object kinds.
template<typename Object, typename Property, typename Defaults, typename Table>
Object* Select(Object* subject, Property&& property, Defaults&& defaults, Table&& table, std::string& error) {
    error.clear();
    auto* actionClass = subject ? property(subject, "ActionSetClass") : nullptr;
    if (!actionClass) { error = "subject has no legacy ActionSetClass"; return nullptr; }
    auto* actionDefaults = defaults(actionClass);
    if (!actionDefaults) { error = "legacy ActionSetClass is not a loaded class with defaults"; return nullptr; }
    auto* selected = property(actionDefaults, "ActionSetTable(EnsureYouCleanOutYourActionSetScript!)");
    if (!selected || !table(selected)) { error = "legacy action defaults have no Struct_ActionSet DataTable"; return nullptr; }
    return selected;
}
template<typename Report> void SkipUntested(Report& report, const std::string& reason) {
    for (const char* name : {"actions", "parts", "kill"}) {
        auto& stage = report.Begin(name); stage.Status = "skipped";
        stage.Detail = std::string(name) == "actions" ? reason : "not reached because actions did not run";
    }
    if (report.Verdict == "running") { report.Verdict = "incomplete"; report.Reason = reason + "; actions, parts and kill untested"; }
}
}
