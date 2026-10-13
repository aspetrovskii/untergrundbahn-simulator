#pragma once

#include "api/Types.h"

#include <cmath>
#include <cstdint>

namespace ubahn::core {

// 03:00. Warmup starts at this time of day, or on the previous day when the period
// starts earlier (ALGORITHM 14.1).
inline constexpr int32_t kWarmupTod = 3 * 3600;

// Positive divisor. The quotient is rounded toward +infinity.
inline int64_t ceilDiv(int64_t value, int64_t divisor) {
    const int64_t quotient = value / divisor;
    const int64_t remainder = value % divisor;
    if (remainder != 0 && value > 0) {
        return quotient + 1;
    }
    return quotient;
}

// Positive divisor. The quotient is rounded toward -infinity, including for negative values.
inline int64_t floorDiv(int64_t value, int64_t divisor) {
    const int64_t quotient = value / divisor;
    const int64_t remainder = value % divisor;
    if (remainder != 0 && value < 0) {
        return quotient - 1;
    }
    return quotient;
}

// Remainder in [0, divisor). Positive divisor.
inline int64_t floorMod(int64_t value, int64_t divisor) {
    const int64_t remainder = value % divisor;
    if (remainder < 0) {
        return remainder + divisor;
    }
    return remainder;
}

// Smallest multiple of step that is >= value. Positive step.
inline int64_t roundUpTo(int64_t value, int64_t step) {
    return ceilDiv(value, step) * step;
}

// Calendar day containing t. Floor division, so a negative t stays on the previous day.
inline int64_t dayIndex(api::SimTime t) {
    return floorDiv(t, api::kSecondsPerDay);
}

// Midnight SimTime of the calendar day containing t.
inline api::SimTime dayStart(api::SimTime t) {
    return dayIndex(t) * static_cast<api::SimTime>(api::kSecondsPerDay);
}

inline int32_t todOf(api::SimTime t) {
    return static_cast<int32_t>(floorMod(t, api::kSecondsPerDay));
}

// 0 = Monday .. 6 = Sunday. Floor modulo, so t < 0 does not follow api::dayOfWeek (it truncates).
inline int dowOf(api::SimTime t) {
    return static_cast<int>(floorMod(dayIndex(t), 7));
}

inline api::SimTime warmupFrom(api::SimTime periodStart) {
    api::SimTime midnight = dayStart(periodStart);
    if (todOf(periodStart) < kWarmupTod) {
        midnight -= api::kSecondsPerDay;
    }
    return midnight + kWarmupTod;
}

// ceil with a 1e-9 tolerance: binary noise just above an integer must not add a second
// (100 * 1.3 stays 130). ALGORITHM 2.
inline int64_t ceilEps(double value) {
    return static_cast<int64_t>(std::ceil(value - 1e-9));
}

} // namespace ubahn::core
