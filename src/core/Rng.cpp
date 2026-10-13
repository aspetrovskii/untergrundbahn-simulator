#include "core/Rng.h"

namespace ubahn::core {

Rng::Rng(uint64_t seed) : engine(seed) {}

uint64_t Rng::next() {
    // TODO(BE-02)
    return 0;
}

int32_t Rng::uniformInt(int32_t, int32_t) {
    // TODO(BE-02)
    return 0;
}

double Rng::uniform01() {
    // TODO(BE-02)
    return 0;
}

uint64_t randomSeed() {
    // TODO(BE-02)
    return 0;
}

} // namespace ubahn::core
