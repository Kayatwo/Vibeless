#include "../PortraitDiscoveryPolicy.h"
#include <cassert>
#include <iostream>

int main() {
    using PortraitDiscoveryPolicy::CanReplace;
    assert(CanReplace(false, true, 1.0f, false, true, true, true));
    assert(!CanReplace(false, false, 1.0f, false, true, true, true));
    assert(!CanReplace(false, true, 0.0f, false, true, true, true));
    assert(!CanReplace(false, true, 0.5f, false, true, true, true));
    assert(!CanReplace(false, true, 1.0f, true, true, true, true));
    assert(!CanReplace(false, true, 1.0f, false, false, true, true));
    assert(!CanReplace(false, true, 1.0f, false, true, false, true));
    assert(!CanReplace(false, true, 1.0f, false, true, true, false));
    // Discovery permits repairing any broken presentation once revealed.
    for (unsigned flags = 0; flags < 64; ++flags)
        assert(CanReplace(true, flags & 1, (flags & 2) ? 1.0f : 0.0f,
            flags & 4, flags & 8, flags & 16, flags & 32));
    struct Color { float R, G, B, A; };
    assert(PortraitDiscoveryPolicy::IsWhite(Color{1, 1, 1, 1}));
    assert(!PortraitDiscoveryPolicy::IsWhite(Color{0, 0, 0, 1}));
    assert(!PortraitDiscoveryPolicy::IsWhite(Color{1, 1, 1, 0}));
    std::cout << "PASS portrait discovery (75 assertions)\n";
}
