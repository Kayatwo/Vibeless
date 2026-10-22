// Bounded diagnostic state for the probe's own subjects. No action values or behavior trees change.
// ProcessEvent is not an exhaustive Blueprint/native call tracer; missing calls stay inconclusive.
// AGPL-3.0-only.
#pragma once
#include "QaProbePolicy.h"
#include "QaProbeActionRoute.h"
#include "SDK/AIModule_classes.hpp"

namespace QaProbeActionTrace {
using namespace SDK;
struct Target { AArchonBehemoth* Actor; int32 Index; std::string Role; };
inline std::vector<Target> Targets;
inline UClass* SpawningClass = nullptr;
inline std::string SpawningRole;
inline bool Querying = false;

inline void Register(AArchonBehemoth* actor, const std::string& role) {
    if (!actor) return;
    for (const auto& t : Targets) if (t.Actor == actor) return;
    if (Targets.size() < 2) Targets.push_back({actor, actor->Index, role});
}
inline FProperty* Property(UStruct* type, const std::string& name, const char* kind, int size) {
    unsigned visited = 0;
    for (auto* owner = type; owner; owner = owner->SuperStruct) {
        if (++visited > 2048) return nullptr;
        for (auto* field = owner->ChildProperties; field; field = field->Next) {
            if (++visited > 2048) return nullptr;
            if (field->Name.ToString() != name) continue;
            auto* p = static_cast<FProperty*>(field);
            return field->ClassPrivate && field->ClassPrivate->Name.ToString() == kind
                && p->ElementSize == size && p->ArrayDim == 1 && p->Offset >= 0
                && p->Offset <= type->Size - size ? p : nullptr;
        }
    }
    return nullptr;
}
inline void Error(QaProbePolicy::ActionTrace& t, const char* message) {
    if (!t.Error.empty()) t.Error += "; ";
    t.Error += message;
}
inline std::optional<bool> Flag(AArchonBehemoth* actor, const char* name) {
    QaProbeActionRoute::Call call(actor, name, {{"ReturnValue", "BoolProperty", 1, 0}});
    if (!call.Function) return std::nullopt;
    call.Run(actor);
    return call.At<bool>();
}
inline void Components(AArchonBehemoth* actor, QaProbePolicy::ActionTrace& t) {
    int count = 0;
    for (AActor* owner : {static_cast<AActor*>(actor), static_cast<AActor*>(actor->Controller)}) {
        if (!owner) continue;
        auto components = owner->K2_GetComponentsByClass(UActorComponent::StaticClass());
        const bool valid = components.Num() >= 0 && components.Num() <= 128;
        if (valid) for (auto* component : components) {
            if (!component || component->GetOwner() != owner) {
                Error(t, "component ownership mismatch"); continue;
            }
            ++count;
            t.Components.push_back(std::string(owner == actor ? "actor:" : "controller:")
                + component->GetName() + "=" + FeatureSupport::ObjectPath(component->Class));
        }
        components.Free();
        if (!valid) { Error(t, "component inventory exceeded bound"); return; }
    }
    t.ComponentCount = count;
}
inline void State(AArchonBehemoth* actor, QaProbePolicy::ActionTrace& t) {
    t.ObjectClass = FeatureSupport::ObjectPath(actor->Class);
    auto* controller = actor->Controller;
    t.Controller = controller ? FeatureSupport::ObjectPath(controller->Class) : "";
    auto* brain = controller && controller->IsA(AAIController::StaticClass()) ? static_cast<AAIController*>(controller)->BrainComponent : nullptr;
    t.Brain = brain ? FeatureSupport::ObjectPath(brain->Class) : "";
    auto* tree = Property(actor->Class, "BehaviorTree", "ObjectProperty", sizeof(UObject*));
    if (tree) t.BehaviorTree = FeatureSupport::ObjectPath(*reinterpret_cast<UObject**>(reinterpret_cast<uint8*>(actor) + tree->Offset));
    else Error(t, "BehaviorTree property unavailable");
    struct ArrayView { uint8* Data; int32 Count, Capacity; };
    auto* overrides = Property(actor->Class, "BehaviorOverrides", "ArrayProperty", sizeof(ArrayView));
    if (!overrides) { Error(t, "BehaviorOverrides property unavailable"); return; }
    const auto& array = *reinterpret_cast<const ArrayView*>(reinterpret_cast<const uint8*>(actor) + overrides->Offset);
    if (array.Count < 0 || array.Count > 64 || array.Count > array.Capacity || (array.Count && !array.Data)) {
        Error(t, "invalid BehaviorOverrides array"); return;
    }
    t.OverrideCount = array.Count;
    if (!array.Count) return;
    auto* inner = static_cast<FArrayProperty*>(overrides)->InnerProperty;
    if (!inner || !inner->ClassPrivate || inner->ClassPrivate->Name.ToString() != "StructProperty"
        || inner->ElementSize <= 0 || inner->ElementSize > 4096 || inner->ArrayDim != 1) {
        Error(t, "BehaviorOverrides element signature unavailable"); return;
    }
    auto* type = static_cast<FStructProperty*>(inner)->Struct;
    if (!type || type->Size != inner->ElementSize) { Error(t, "BehaviorOverrides struct size mismatch"); return; }
    auto* asset = Property(type, "BehaviorTreeAsset_5_FF632CD44198B8803CB2849BB843AF88", "ObjectProperty", sizeof(UObject*));
    auto* tag = Property(type, "Tag_2_1EAEC4044C1E08E14F6011830A8FAD37", "StructProperty", sizeof(FName));
    auto* tagType = tag ? static_cast<FStructProperty*>(tag)->Struct : nullptr;
    auto* tagName = tagType ? Property(tagType, "TagName", "NameProperty", sizeof(FName)) : nullptr;
    if (!asset || !tagName) { Error(t, "BehaviorOverrides tree/tag signature unavailable"); return; }
    for (int32 i = 0; i < array.Count; ++i) {
        const auto* entry = array.Data + i * inner->ElementSize;
        t.Overrides.push_back(reinterpret_cast<const FName*>(entry + tag->Offset + tagName->Offset)->ToString()
            + "=" + FeatureSupport::ObjectPath(*reinterpret_cast<UObject* const*>(entry + asset->Offset)));
    }
}
inline void Snapshot(QaProbePolicy::Report& report, AArchonBehemoth* actor, const std::string& role,
    const std::string& point, UClass* actionClass = nullptr, const std::string& row = {},
    std::optional<double> damage = std::nullopt, const std::string& error = {}) {
    if (!actor || actor->IsActorBeingDestroyed()) return;
    QaProbePolicy::ActionTrace t;
    t.Role = role; t.Point = point; t.Row = row;
    t.RequestedDamage = damage; t.Error = error;
    State(actor, t);
    const auto health = actor->GetCurrentHealth(), maximum = actor->GetMaxHealth();
    if (std::isfinite(health) && std::isfinite(maximum)) { t.Health = health; t.MaxHealth = maximum; }
    else Error(t, "non-finite health");
    t.Enraged = Flag(actor, "IsEnraged"); t.AetherCharged = Flag(actor, "IsAetherCharged");
    if (!t.Enraged || !t.AetherCharged) Error(t, "rage/aether query signature unavailable");
    Components(actor, t);
    if (actionClass) {
        t.Action = FeatureSupport::ObjectPath(actionClass);
        Querying = true;
        const auto live = QaProbeActionRoute::Find(actor, actionClass);
        if (!live.Error.empty()) Error(t, live.Error.c_str());
        else {
            if (std::isfinite(live.Action->BaseValue)) t.BaseValue = live.Action->BaseValue;
            else Error(t, "non-finite live BaseValue");
            t.Subtree = FeatureSupport::ObjectPath(live.Action->SubtreeForAction);
            t.Available = QaProbeActionRoute::Available(live.Action);
            if (!t.Available) Error(t, "IsActionAvailable signature unavailable");
        }
        Querying = false;
    }
    report.Trace(std::move(t));
}
inline void Observe(QaProbePolicy::Report& report, UObject* object, UFunction* function, bool after) {
    if (!object || !function || report.Finished || report.Side != "server" || report.Id.empty()) return;
    const auto name = function->GetName();
    if (name != "ReceivePossessed" && name != "RunBehaviorTree" && name != "SetupDynamicSubtrees"
        && name != "SetDynamicSubtree") return;
    if (SpawningClass && name == "ReceivePossessed" && object->IsA(SpawningClass)
        && object->IsA(AArchonBehemoth::StaticClass())) Register(static_cast<AArchonBehemoth*>(object), SpawningRole);
    for (const auto& target : Targets) {
        if (UObject::GObjects->GetByIndex(target.Index) != target.Actor) continue;
        auto* actor = target.Actor;
        auto* controller = actor->Controller;
        auto* brain = controller && controller->IsA(AAIController::StaticClass()) ? static_cast<AAIController*>(controller)->BrainComponent : nullptr;
        const bool action = object->IsA(ABP_BehemothAction_C::StaticClass())
            && static_cast<ABP_BehemothAction_C*>(object)->OwningArchonBehemoth == actor;
        if (object != actor && object != controller && object != brain && !action) continue;
        QaProbePolicy::ActionTrace t;
        t.Role = target.Role; t.Function = name;
        t.Point = std::string(Querying ? "explicit-query-" : "observed-") + (after ? "after" : "before");
        State(actor, t);
        t.ObjectClass = FeatureSupport::ObjectPath(object->Class);
        report.Trace(std::move(t));
        return;
    }
}
}
