#include "core/Line.h"

#include "api/Functions.h"
#include "core/Time.h"

#include <algorithm>
#include <cmath>

namespace ubahn::core {
namespace {

// SPEC C1, C2. 80 km/h and 1 m/s²; the coasting distance is v²/(2a) ≈ 246.9 m.
constexpr double kVMaxMps = 80.0 / 3.6;
constexpr double kAccelMps2 = 1.0;

// SPEC C3: ⌈tStd / 1.5⌉. 1.5 = 3/2, so this is ⌈2·tStd / 3⌉ without a double multiply.
int32_t ceilStdOverOneAndHalf(int32_t tStd) {
    return static_cast<int32_t>(ceilDiv(static_cast<int64_t>(tStd) * 2, 3));
}

Span spanFromInput(const api::SpanInput& input, double kSlow) {
    Span span;
    span.lengthM = input.lengthM;
    span.tStd = input.tStd.value_or(api::defaultStdRunTime(input.lengthM));
    // C3: the coefficient and the physical minimum; the larger one wins.
    span.tMin = std::max(ceilStdOverOneAndHalf(span.tStd), api::physMinRunTime(span.lengthM));
    // C4. ceilEps, so an integer product such as 100 × 1.3 stays 130.
    span.tMax = static_cast<int32_t>(ceilEps(static_cast<double>(span.tStd) * kSlow));
    return span;
}

} // namespace

double tPhys(double lengthM) {
    // ALGORITHM 3.1. At L = 2·sAcc the two branches meet: 2·v/a.
    const double sAcc = (kVMaxMps * kVMaxMps) / (2.0 * kAccelMps2);
    if (lengthM <= 2.0 * sAcc) {
        return 2.0 * std::sqrt(lengthM / kAccelMps2);
    }
    return (2.0 * kVMaxMps) / kAccelMps2 + (lengthM - 2.0 * sAcc) / kVMaxMps;
}

Line buildLine(const api::LineConfig& cfg) {
    Line line;
    line.n = static_cast<int>(cfg.stationNames.size());
    line.cfg = cfg;
    line.spans.reserve(cfg.spans.size());
    for (const api::SpanInput& input : cfg.spans) {
        line.spans.push_back(spanFromInput(input, cfg.kSlow));
    }
    return line;
}

const Span& spanOf(const Line& line, api::Dir dir, int pos) {
    // ALGORITHM 3.2. Path II walks the same spans from the other end.
    const int index = dir == api::Dir::I ? pos : line.n - 2 - pos;
    return line.spans[static_cast<size_t>(index)];
}

int stationOf(const Line& line, api::Dir dir, int pos) {
    return dir == api::Dir::I ? pos : line.n - 1 - pos;
}

int posOf(const Line& line, api::Dir dir, int station) {
    return dir == api::Dir::I ? station : line.n - 1 - station;
}

} // namespace ubahn::core

namespace ubahn::api {

int32_t physMinRunTime(double lengthM) {
    return static_cast<int32_t>(core::ceilEps(core::tPhys(lengthM)));
}

int32_t defaultStdRunTime(double lengthM) {
    // SPEC 5: 1.5 × t_phys, up to the next second, then up to a multiple of 5 s.
    const int64_t scaled = core::ceilEps(1.5 * core::tPhys(lengthM));
    return static_cast<int32_t>(core::roundUpTo(scaled, 5));
}

int32_t minRunTime(const LineConfig& line, int span) {
    if (span < 0 || static_cast<size_t>(span) >= line.spans.size()) {
        return 0;
    }
    return core::buildLine(line).spans[static_cast<size_t>(span)].tMin;
}

int32_t maxRunTime(const LineConfig& line, int span) {
    if (span < 0 || static_cast<size_t>(span) >= line.spans.size()) {
        return 0;
    }
    return core::buildLine(line).spans[static_cast<size_t>(span)].tMax;
}

} // namespace ubahn::api
