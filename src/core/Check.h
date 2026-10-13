#pragma once

#include <cassert>

namespace ubahn::core {

// Set by CMake on ubahn_core for the Debug configuration only.
#if defined(UBAHN_CHECK_INVARIANTS)
inline constexpr bool kCheckInvariants = true;
#else
inline constexpr bool kCheckInvariants = false;
#endif

} // namespace ubahn::core

// msg is a string literal; it is part of the assertion text. Release builds drop the check.
#if defined(UBAHN_CHECK_INVARIANTS)
#define UBAHN_ASSERT(cond, msg) assert((cond) && (msg))
#else
#define UBAHN_ASSERT(cond, msg) ((void)0)
#endif
