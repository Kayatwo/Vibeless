// Shared helpers for the Vibeless native features: profile scope, JSON text for their logs, and
// small reflection lookups. Split from the retired developer arena probe (Phase 0 hazard H2).
// AGPL-3.0-only.
#pragma once
// Standard headers the feature headers have always received through this include.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "ProfileRoot.h"

namespace FeatureSupport {
using namespace SDK;
// Profile root of this game process, or empty outside a Vibeless profile (see ProfileRoot.h).
inline const std::wstring& Root() { return ProfileRoot::Root(); }
// Features act only inside a Vibeless profile (the feature host also keeps them off outside one).
inline bool IsExpectedProcess() { return !Root().empty(); }
inline std::string Json(const std::string& value) {
    std::string result = "\"";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { result += '\\'; result += static_cast<char>(ch); }
        else if (ch == '\n') result += "\\n";
        else if (ch == '\r') result += "\\r";
        else if (ch == '\t') result += "\\t";
        else if (ch >= 32) result += static_cast<char>(ch);
    }
    return result + "\"";
}
inline const char* Bool(bool value) { return value ? "true" : "false"; }
inline std::string Trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
inline std::string ObjectPath(UObject* object) {
    if (!object) return {};
    auto* package = object;
    while (package->Outer) package = package->Outer;
    return package->Name.GetRawString() + "." + object->GetName();
}
inline UFunction* FindFunction(UObject* object, const std::string& name) {
    for (UStruct* type = object ? object->Class : nullptr; type; type = type->SuperStruct)
        for (UField* field = type->Children; field; field = field->Next)
            if (field->HasTypeFlag(EClassCastFlags::Function) && field->GetName() == name) return static_cast<UFunction*>(field);
    return nullptr;
}
// Calls a reflected getter that returns one actor (e.g. a widget's GetOwningPlayer).
inline AActor* ActorOutput(UObject* object, const std::string& name) {
    auto* function = FindFunction(object, name);
    if (!function) return nullptr;
    struct { AActor* Actor = nullptr; } params;
    object->ProcessEvent(function, &params);
    return params.Actor;
}
}
