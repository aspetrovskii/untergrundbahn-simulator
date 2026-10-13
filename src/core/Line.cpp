#include "core/Line.h"

#include "api/Functions.h"

namespace ubahn::core {

double tPhys(double) {
    // TODO(BE-03): ALGORITHM 3.1
    return 0;
}

Line buildLine(const api::LineConfig&) {
    // TODO(BE-03)
    return {};
}

const Span& spanOf(const Line&, api::Dir, int) {
    // TODO(BE-03): ALGORITHM 3.2
    static const Span empty;
    return empty;
}

int stationOf(const Line&, api::Dir, int) {
    // TODO(BE-03)
    return 0;
}

int posOf(const Line&, api::Dir, int) {
    // TODO(BE-03)
    return 0;
}

} // namespace ubahn::core

namespace ubahn::api {

int32_t physMinRunTime(double) {
    // TODO(BE-03)
    return 0;
}

int32_t defaultStdRunTime(double) {
    // TODO(BE-03)
    return 0;
}

int32_t minRunTime(const LineConfig&, int) {
    // TODO(BE-03)
    return 0;
}

int32_t maxRunTime(const LineConfig&, int) {
    // TODO(BE-03)
    return 0;
}

} // namespace ubahn::api
