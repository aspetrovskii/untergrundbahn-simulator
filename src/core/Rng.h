#pragma once

#include <cstdint>
#include <random>

namespace ubahn::core {

// Portable wrapper over std::mt19937_64. std::uniform_*_distribution is not used:
// its sequences depend on the standard library (decision R9).
class Rng {
public:
    explicit Rng(uint64_t seed);

    uint64_t next();
    // Inclusive on both ends.
    int32_t uniformInt(int32_t lo, int32_t hi);
    // Half-open [0, 1).
    double uniform01();

private:
    std::mt19937_64 engine;
};

// std::random_device. Call only when the run was given no seed.
uint64_t randomSeed();

} // namespace ubahn::core
