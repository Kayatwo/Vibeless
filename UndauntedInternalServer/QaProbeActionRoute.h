// Legacy action-class route used by BTTask_PerformActionClass: use the action already created by
// the subject's ActionSet, run its pre/choose/play/post calls, and retain unresolved routes as failures.
// This does not reconstruct compound behavior-tree branches or provide a participating target.
// AGPL-3.0-only.
#pragma once
#include "FeatureSupport.h"
#include "SDK/BP_BehemothAction_classes.hpp"

namespace QaProbeActionRoute {
using namespace SDK;
// Allocate the complete reflected frame, including Blueprint locals. Reject changed signatures
// before ProcessEvent; SDK declarations alone do not describe private Blueprint overrides.
struct Parameter { const char* Name; const char* Type; int Size; int Offset; };
struct alignas(16) FrameBlock { uint8 Bytes[16]{}; };
struct Call {
    UFunction* Function = nullptr;
    std::vector<FrameBlock> Frame;
    Call(UObject* object, const char* name, std::initializer_list<Parameter> expected) {
        auto* fn = FeatureSupport::FindFunction(object, name);
        if (!fn || fn->Size < 0 || fn->Size > 65536) return;
        size_t count = 0;
        unsigned fields = 0;
        for (auto* field = fn->ChildProperties; field; field = field->Next) {
            if (++fields > 2048) return;
            auto* p = static_cast<FProperty*>(field);
            if (!(p->PropertyFlags & static_cast<uint64>(EPropertyFlags::Parm))) continue;
            bool match = false;
            for (const auto& e : expected)
                if (field->Name.ToString() == e.Name && field->ClassPrivate
                    && field->ClassPrivate->Name.ToString() == e.Type && p->ArrayDim == 1
                    && p->ElementSize == e.Size && p->Offset == e.Offset
                    && p->Offset >= 0 && p->Offset <= fn->Size - e.Size) match = true;
            if (!match) return;
            ++count;
        }
        if (count != expected.size()) return;
        Function = fn;
        Frame.resize((static_cast<size_t>(fn->Size) + 15) / 16 + 1);
    }
    template<class T> T& At(size_t offset = 0) { return *reinterpret_cast<T*>(reinterpret_cast<uint8*>(Frame.data()) + offset); }
    void Run(UObject* object) { object->ProcessEvent(Function, Frame.data()); }
};
struct Live {
    ABP_BehemothAction_C* Action = nullptr;
    int32 Index = -1;
    UAnimMontage* Montage = nullptr;
    bool Prepared = false;
    float Length = 0;
    std::string Error;
};
inline bool Alive(const Live& live) {
    return live.Action && UObject::GObjects->GetByIndex(live.Index) == live.Action && !live.Action->IsActorBeingDestroyed();
}
inline void Finish(Live& live) {
    if (live.Prepared && !Alive(live)) live.Error = "live action was destroyed during execution";
    if (live.Prepared && Alive(live)) {
        Call post(live.Action, "PostAttackLogic", {});
        if (post.Function) post.Run(live.Action);
        else live.Error = "PostAttackLogic signature unavailable";
        live.Action->InProgress = false;
    }
    live.Prepared = false;
}
inline Live Begin(AArchonBehemoth* monster, UClass* actionClass) {
    Live live;
    Call actions(monster, "GetActionArray", {{"ActionArray", "ArrayProperty", sizeof(TArray<AActor*>), 0}});
    if (!actions.Function) { live.Error = "GetActionArray signature unavailable"; return live; }
    actions.Run(monster);
    auto& array = actions.At<TArray<AActor*>>();
    if (array.Num() < 0 || array.Num() > 256) { live.Error = "invalid live action array"; return live; }
    for (auto* actor : array) {
        if (!actor || actor->Class != actionClass || !actor->IsA(ABP_BehemothAction_C::StaticClass())) continue;
        auto* action = static_cast<ABP_BehemothAction_C*>(actor);
        if (action->OwningArchonBehemoth != monster || action->IsActorBeingDestroyed()) continue;
        if (live.Action) { live.Error = "ambiguous live action class"; break; }
        live.Action = action; live.Index = action->Index;
    }
    // GetActionArray copies an engine array into the out parameter. The SDK's allocator-aware Free
    // returns that buffer to the engine, just as a generated parameter object's destructor does.
    array.Free();
    if (!live.Error.empty()) return live;
    if (!live.Action) { live.Error = "no matching live action owned by subject"; return live; }
    Call pre(live.Action, "PreAttackLogic", {}), post(live.Action, "PostAttackLogic", {});
    Call choose(live.Action, "ChooseMontageToPlay", {{"Montage", "ObjectProperty", sizeof(UAnimMontage*), 0}});
    Call play(monster, "PlayBehemothMontage", {
        {"BehemothMontage", "ObjectProperty", 8, 0}, {"MontagePlayRate", "FloatProperty", 4, 8},
        {"StartSection", "NameProperty", sizeof(FName), 12}, {"Duration", "FloatProperty", 4, 20}});
    if (!pre.Function || !post.Function || !choose.Function || !play.Function) {
        live.Error = "legacy pre/choose/play/post signature unavailable"; return live;
    }
    if (!std::isfinite(live.Action->MontagePlayRate) || live.Action->MontagePlayRate <= 0 || live.Action->MontagePlayRate >= 10) {
        live.Error = "invalid live action montage rate"; return live;
    }
    pre.Run(live.Action); live.Prepared = true;
    choose.Run(live.Action);
    live.Montage = choose.At<UAnimMontage*>();
    if (!live.Montage) { live.Error = "no action-selected montage; compound behavior-tree route unresolved"; Finish(live); return live; }
    if (!live.Montage->IsA(UAnimMontage::StaticClass())) { live.Error = "action returned a non-montage"; Finish(live); return live; }
    play.At<UAnimMontage*>() = live.Montage;
    play.At<float>(8) = live.Action->MontagePlayRate;
    play.Run(monster);
    live.Length = play.At<float>(20);
    if (!std::isfinite(live.Length) || live.Length <= 0) { live.Error = "actor PlayBehemothMontage did not start"; Finish(live); }
    return live;
}
}
