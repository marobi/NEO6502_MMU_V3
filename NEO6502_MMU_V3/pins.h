#pragma once

#include "hardware/structs/sio.h"

// --------------------------------------------------------------------------------
// Compile-time fast pin abstraction supporting pins >= 32
// Supports RP2040 / RP2350 style dual 32-bit GPIO banks (0-29 on RP2040, up to 47+ on RP2350 variants)
//
constexpr uint32_t GPIO_BANK_WIDTH = 32u;

template<uint32_t Pin>
struct FastPin
{
  static_assert(Pin < 64u, "Pin out of supported range");

  static constexpr uint32_t BANK = Pin / GPIO_BANK_WIDTH;
  static constexpr uint32_t BIT = Pin % GPIO_BANK_WIDTH;
  static constexpr uint32_t MASK = 1u << BIT;

  static inline __attribute__((always_inline))
    void high() {
    if constexpr (BANK == 0)
      sio_hw->gpio_set = MASK;
    else
      sio_hw->gpio_hi_set = MASK;
  }

  static inline __attribute__((always_inline))
    void low() {
    if constexpr (BANK == 0)
      sio_hw->gpio_clr = MASK;
    else
      sio_hw->gpio_hi_clr = MASK;
  }

  static inline __attribute__((always_inline))
    void set(bool v) {
    if (v)
      high();
    else
      low();
  }
};

/*
Usage example:

constexpr uint32_t pDebug = 38u;   // bank 1 (>=32)
using DebugPin = FastPin<pDebug>;

DebugPin::high();
DebugPin::low();
*/

//-------------------------------------------------------------------------------
// 
// the control pins
constexpr uint32_t pRW = 4u;
using PRWPin = FastPin<pRW>;

constexpr uint32_t mRW = 5u;
using MRWPin = FastPin<mRW>;

constexpr uint32_t pMMUIO = 7u;
using MMUIOPin = FastPin<pMMUIO>;

constexpr uint32_t pMMUARegHLatch = 22u;
using MMUARegHLatchPin = FastPin<pMMUARegHLatch>;

constexpr uint32_t pMMUDRegOE = 23u;
using MMUDRegOEPin = FastPin<pMMUDRegOE>;

constexpr uint32_t p6502PHI2 = 26u;
using PHI2Pin = FastPin<p6502PHI2>;

constexpr uint32_t p6502RESET = 27u;
using RESETPin = FastPin<p6502RESET>;

constexpr uint32_t p6502RW = 28u;
using RWPin = FastPin<p6502RW>;

constexpr uint32_t p6502BE = 29u;
using BEPin = FastPin<p6502BE>;

constexpr uint32_t p6502RDY = 30u;
using RDYPin = FastPin<p6502RDY>;

constexpr uint32_t p6502IRQ = 31u;
using IRQPin = FastPin<p6502IRQ>;

constexpr uint32_t pCPUARegLLatch = 32u;
using CPUARegLLatchPin = FastPin<pCPUARegLLatch>;

constexpr uint32_t pCPUARegHLatch = 33u;
using CPUARegHLatchPin = FastPin<pCPUARegHLatch>;

constexpr uint32_t pCPUARegOE = 34u;
using CPUARegOEPin = FastPin<pCPUARegOE>;

constexpr uint32_t pCPUABufLOE = 35u;
using CPUABufLOEPin = FastPin<pCPUABufLOE>;

constexpr uint32_t pCPUABufHOE = 36u;
using CPUABufHOEPin = FastPin<pCPUABufHOE>;

constexpr uint32_t pCPUDBufOE = 37u;
using CPUDBufOEPin = FastPin<pCPUDBufOE>;

constexpr uint32_t pDebug = 38u;
using DebugPin = FastPin<pDebug>;
