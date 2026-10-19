// Which UDP ports are hunt servers, derived from the deploy server's port range. AGPL-3.0-only.
//
// The deploy server gives Ramsgate PORT_RANGE_END, the Training Grounds PORT_RANGE_END-1 and hunts
// the ports from PORT_RANGE_BEGIN to PORT_RANGE_END-2, and its game servers inherit those variables.
// Without them (or with invalid values) the BackportDev lab layout 8760-8767 applies, so the
// installed release keeps its behaviour. The parser is portable; LoadFromEnvironment uses Windows.
#pragma once
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

namespace HuntPortPolicy {
struct Range { int Begin = 8760, End = 8767; };
inline Range Active{};

// Both values must parse completely, lie in 1024-65535 and leave room for at least one hunt.
inline Range Parse(const char* begin, const char* end) {
    if (!begin || !end || !*begin || !*end) return {};
    char* b = nullptr; char* e = nullptr;
    const long first = std::strtol(begin, &b, 10), last = std::strtol(end, &e, 10);
    if (*b || *e || first < 1024 || last > 65535 || last - first < 2) return {};
    return { static_cast<int>(first), static_cast<int>(last) };
}
inline bool IsHunt(int port, const Range& range = Active) { return port >= range.Begin && port <= range.End - 2; }
inline int TrainingGrounds(const Range& range = Active) { return range.End - 1; }
inline int Ramsgate(const Range& range = Active) { return range.End; }

#ifdef _WIN32
// Call once in a game server before its first tick.
inline void LoadFromEnvironment() {
    char begin[16] = {}, end[16] = {};
    const DWORD b = GetEnvironmentVariableA("PORT_RANGE_BEGIN", begin, sizeof(begin));
    const DWORD e = GetEnvironmentVariableA("PORT_RANGE_END", end, sizeof(end));
    Active = Parse(b > 0 && b < sizeof(begin) ? begin : nullptr, e > 0 && e < sizeof(end) ? end : nullptr);
}
#endif
}
