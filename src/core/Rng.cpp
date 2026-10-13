#include "core/Rng.h"

#include "core/Check.h"

namespace ubahn::core {

Rng::Rng(uint64_t seed) : engine(seed) {}

uint64_t Rng::next() {
    return engine();
}

int32_t Rng::uniformInt(int32_t lo, int32_t hi) {
    // Inclusive on both ends, without modulo bias (decision R9). std::uniform_int_distribution
    // is not used: its sequence depends on the standard library.
    UBAHN_ASSERT(lo <= hi, "uniformInt lo <= hi");
    if (hi < lo) {
        const int32_t swapped = lo;
        lo = hi;
        hi = swapped;
    }
    const uint64_t span =
        static_cast<uint64_t>(static_cast<int64_t>(hi) - static_cast<int64_t>(lo));
    const uint64_t bound = span + 1;
    // threshold = 2^64 % bound. Draws below it are rejected so the kept range is a multiple of
    // bound.
    const uint64_t threshold = (static_cast<uint64_t>(0) - bound) % bound;
    uint64_t value = next();
    while (value < threshold) {
        value = next();
    }
    const int64_t picked = static_cast<int64_t>(lo) + static_cast<int64_t>(value % bound);
    return static_cast<int32_t>(picked);
}

double Rng::uniform01() {
    // 53 bits of the mantissa. (next() >> 11) * 2^-53 lies in [0, 1) (decision R9).
    return static_cast<double>(next() >> 11) * 0x1.0p-53;
}

uint64_t randomSeed() {
    // operator() yields unsigned int. Two draws fill a uint64_t (risk R17 is checked in tst_rng).
    std::random_device device;
    const uint64_t high = static_cast<uint64_t>(device()) & 0xffffffffu;
    const uint64_t low = static_cast<uint64_t>(device()) & 0xffffffffu;
    return (high << 32) | low;
}

} // namespace ubahn::core
