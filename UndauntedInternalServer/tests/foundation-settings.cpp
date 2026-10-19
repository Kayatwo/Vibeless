#include "../FoundationSettings.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace FoundationSettings;
    for (auto text : {L"1", L"true", L"ON", L" yes "}) assert(ParseOnOff(text, false));
    for (auto text : {L"0", L"false", L"Off", L"NO"}) assert(!ParseOnOff(text, true));
    for (auto text : {L"", L"maybe", L"2"}) { assert(ParseOnOff(text, true)); assert(!ParseOnOff(text, false)); }

    assert(ParseFusionHours(L"") == DefaultFusionHours);
    assert(ParseFusionHours(L"retail") == RetailFusionHours);
    assert(ParseFusionHours(L"RETAIL") == RetailFusionHours);
    assert(ParseFusionHours(L"0") == RetailFusionHours);
    assert(ParseFusionHours(L"6") == 6);
    assert(ParseFusionHours(L"720") == 720);
    for (auto text : {L"721", L"-3", L"abc", L"6h", L"1.5"}) assert(ParseFusionHours(text) == DefaultFusionHours);

    std::cout << "foundation-settings: all assertions passed\n";
    return 0;
}
