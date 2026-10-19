#include "../HuntPortPolicy.h"
#include <cassert>
#include <iostream>
#include <utility>

int main() {
    using namespace HuntPortPolicy;
    // Lab layout by default: hunts 8760-8765, Training Grounds 8766, Ramsgate 8767.
    for (int port = 8760; port <= 8765; ++port) assert(IsHunt(port));
    for (int port : {8759, 8766, 8767, 0}) assert(!IsHunt(port));
    assert(TrainingGrounds() == 8766 && Ramsgate() == 8767);

    // Another deploy layout, e.g. upstream's 8770-8777.
    const Range upstream = Parse("8770", "8777");
    assert(upstream.Begin == 8770 && upstream.End == 8777);
    assert(IsHunt(8770, upstream) && IsHunt(8775, upstream) && !IsHunt(8776, upstream) && !IsHunt(8765, upstream));
    assert(TrainingGrounds(upstream) == 8776 && Ramsgate(upstream) == 8777);

    // Missing or invalid values keep the lab layout.
    for (auto pair : {std::pair<const char*, const char*>{nullptr, "8777"}, {"8770", ""}, {"87x0", "8777"},
                      {"8770", "8771"}, {"80", "90"}, {"8770", "70000"}, {"8777", "8770"}}) {
        const Range range = Parse(pair.first, pair.second);
        assert(range.Begin == 8760 && range.End == 8767);
    }
    std::cout << "hunt-ports: all assertions passed\n";
    return 0;
}
