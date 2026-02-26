#pragma once

// control mode
constexpr auto mCPU     = 1;
constexpr auto mRPI     = 0;

// Bus direction
constexpr auto mWRITE   = false;
constexpr auto mREAD    = true;

// Pin setting
constexpr auto mLOW     = false;
constexpr auto mHIGH    = true;

// pin direction
constexpr auto mINPUT   = false;
constexpr auto mOUTPUT  = true;

#define pRW              (4u)
#define mRW              (5u)

// control MMU I/O page 
#define pMMUIO           (7u)

// MMU control pins
#define pMMUARegHLatch   (22u)
#define pMMUDRegOE       (23u)

// control 6502
#define p6502PHI2        (26u) // PHI2
#define p6502RESET       (27u) // RESB
#define p6502RW          (28u) // R/W
#define p6502BE          (29u) // Bus Enable
#define p6502RDY         (30u) // Ready
#define p6502IRQ         (31u) // IRQ

// RAM control pins
#define pCPUARegLLatch   (32u)
#define pCPUARegHLatch   (33u)
#define pCPUARegOE       (34u)
#define pCPUABufLOE      (35u)
#define pCPUABufHOE      (36u)
#define pCPUDBufOE       (37u)

// control
#define pDebug           (38u)
