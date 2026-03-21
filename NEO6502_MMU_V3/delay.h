#pragma once

#include <stdint.h>
#include "pico/platform.h"   // for busy_wait_at_least_cycles()
#include "config.h"

// --- compile-time conversion ---
constexpr uint32_t nsToCyclesConst(uint32_t ns) {
  return (static_cast<uint64_t>(ns) * CPU_CLOCK_HZ + 999999999ull) / 1000000000ull;
}

// --- template: nanoseconds -> cycles (compile-time only) ---
template <uint32_t NS>
static inline void delayNs()
{
  constexpr uint32_t cycles = nsToCyclesConst(NS);
  static_assert(cycles > 0, "delayNs<NS>: NS too small -> 0 cycles");

  busy_wait_at_least_cycles(cycles);
}

// --- template: direct cycles (compile-time only) ---
template <uint32_t CYCLES>
static inline void delayCycles()
{
  static_assert(CYCLES > 0, "delayCycles<CYCLES>: must be > 0");

  busy_wait_at_least_cycles(CYCLES);
}
