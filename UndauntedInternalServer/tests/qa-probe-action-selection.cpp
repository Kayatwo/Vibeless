#include "../QaProbeActionSelection.h"
#include "../QaProbePolicy.h"
#include <cassert>
#include <map>
#include <string>

struct Object { bool Class = false, Table = false; Object* Defaults = nullptr; std::map<std::string, Object*> Fields; };
int main() {
    Object stockTable{false, true}, privateTable{false, true}, unrelatedInventoryTable{false, true};
    Object stockDefaults, privateDefaults, stockClass{true}, privateClass{true}, stock, creature;
    stockClass.Defaults = &stockDefaults; privateClass.Defaults = &privateDefaults;
    stockDefaults.Fields["ActionSetTable(EnsureYouCleanOutYourActionSetScript!)"] = &stockTable;
    privateDefaults.Fields["ActionSetTable(EnsureYouCleanOutYourActionSetScript!)"] = &privateTable;
    stock.Fields["ActionSetClass"] = &stockClass; creature.Fields["ActionSetClass"] = &privateClass;
    // The similarly named native gameplay ActionSet is an unrelated data asset.
    creature.Fields["ActionSet"] = &unrelatedInventoryTable;
    auto property = [](Object* object, const char* name) { auto p = object->Fields.find(name); return p == object->Fields.end() ? nullptr : p->second; };
    auto defaults = [](Object* object) { return object->Class ? object->Defaults : nullptr; };
    auto table = [](Object* object) { return object->Table; };
    std::string error;
    auto select = [&](Object* actor) { return QaProbeActionSelection::Select(actor, property, defaults, table, error); };
    assert(select(&creature) == &privateTable && error.empty());
    assert(select(&stock) == &stockTable && error.empty());
    assert(select(&creature) != &unrelatedInventoryTable);
    creature.Fields.erase("ActionSetClass"); assert(!select(&creature) && error.find("ActionSetClass") != std::string::npos);
    creature.Fields["ActionSetClass"] = &unrelatedInventoryTable; assert(!select(&creature) && error.find("loaded class") != std::string::npos);
    creature.Fields["ActionSetClass"] = &privateClass; privateTable.Table = false;
    assert(!select(&creature) && error.find("Struct_ActionSet") != std::string::npos);
    assert(!select(nullptr));
    QaProbePolicy::Report report;
    QaProbeActionSelection::SkipUntested(report, "missing subject-selected table");
    assert(report.Verdict == "incomplete" && report.Stages.size() == 3);
    for (const auto& stage : report.Stages) assert(stage.Status == "skipped");
    assert(report.Stages[0].Name == "actions" && report.Stages[1].Name == "parts" && report.Stages[2].Name == "kill");
    assert(report.Actions.empty() && report.Parts.empty() && report.Kills.empty());
    QaProbePolicy::Report failed; failed.Verdict = "fail-sample"; failed.Reason = "frozen";
    QaProbeActionSelection::SkipUntested(failed, "prior stage failed");
    assert(failed.Verdict == "fail-sample" && failed.Reason == "frozen");
}
