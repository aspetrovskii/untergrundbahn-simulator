#pragma once

#include "api/Types.h"

#include <cstdint>
#include <vector>

namespace ubahn::core {

// One inter-station span, shared by both directions. ALGORITHM 3.1.
struct Span {
    double lengthM = 0;
    int32_t tStd = 0;
    int32_t tMin = 0;
    int32_t tMax = 0;
};

// Plain aggregate: tests build it by hand. n is the station count.
struct Line {
    int n = 0;
    std::vector<Span> spans;
    api::LineConfig cfg;
};

// Physical minimum running time in seconds, not yet rounded. ALGORITHM 3.1.
double tPhys(double lengthM);

// Fills tStd (the default when SpanInput::tStd is empty), tMin and tMax.
Line buildLine(const api::LineConfig& cfg);

// Span for position pos along dir. Dir::II reads spans from the opposite end. ALGORITHM 3.2.
const Span& spanOf(const Line& line, api::Dir dir, int pos);

// Physical station for a position along dir. Dir::II maps pos 0 to station n - 1.
int stationOf(const Line& line, api::Dir dir, int pos);

// Position along dir for a physical station. Inverse of stationOf.
int posOf(const Line& line, api::Dir dir, int station);

} // namespace ubahn::core
