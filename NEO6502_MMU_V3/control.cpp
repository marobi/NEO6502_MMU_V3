// 
// 
// 
#include <arduino.h>
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"

#include "pins.h"
#include "control.h"
#include "mmu.h"
#include "p6502.h"

// neo bus
constexpr auto DATA_BUS_MASK = (0xFF0000000000LL); // pin 40..47


// maintains control
static uint8_t gControlMode = mRPI;

// maintains databus direction
static bool gBusDir = mINPUT;  // invalid :-)

/// <summary>
/// 
/// </summary>
/// <param name="vRW"></param>
void setDebug(const bool vRW) {
  gpio_put(pDebug, vRW);
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint8_t getControlMode() {
  return (gControlMode);
}

/// <summary>
/// set mode of control
/// </summary>
/// <param name="vMode"></param>
void setControlMode(const uint8_t vMode) {
  gControlMode = vMode;
}

/// <summary>
/// 
/// </summary>
/// <param name="vRW"></param>
void setmRW(const bool vRW) {
  gpio_put(mRW, vRW);
}

/// <summary>
/// set GPIO as input/output
/// </summary>
/// <param name="lDirection"></param>
inline __attribute__((always_inline))
void  setNEOBusDir(const bool lDirection) {
  if (lDirection != gBusDir) {
    switch (lDirection) {
    case mWRITE:  
      gpio_set_dir_masked64(DATA_BUS_MASK, DATA_BUS_MASK);
      break;

    case mREAD:   
      gpio_set_dir_masked64(DATA_BUS_MASK, (uint64_t)0ULL);
      break;
    }

    gBusDir = lDirection;
  }
}

/// <summary>
/// read a byte from NEObus
/// </summary>
/// <returns></returns>
uint8_t readNEOBus() {
  setNEOBusDir(mREAD);     // input

  DELAY_FACTOR_SHORT();

  uint32_t lData = ((uint32_t)sio_hw->gpio_hi_in);
//  Serial.printf("*D: readNEOBus: 0x%08lX\n", lData);

  return (lData >> 8u) & 0xFF;
}

/// <summary>
/// write a byte to the NEObus
/// </summary>
/// <param name="vData"></param>
void writeNEOBus(const uint8_t vData) {
  setNEOBusDir(mWRITE);

  uint64_t lData = (uint64_t)vData;
  lData = lData << 40u; // align to pin 40..47
//  Serial.printf("*D: writeNEOBus2: 0x%02X =>0x%08llX\n", vData, lData);
  gpio_put_masked64(DATA_BUS_MASK, lData);

  DELAY_FACTOR_SHORT();
}

/// <summary>
/// reset NOEObus
/// </summary>
void resetNEOBus() {
  setNEOBusDir(mREAD);
}

/// <summary>
/// 
/// </summary>
void setupControl() {
  // NEO databus init
  for (uint8_t  i = (40u); i <= (47u); i++) {
    gpio_init(i);                  // Always init pins first
    gpio_set_dir(i, GPIO_IN);      // Set as input
    gpio_pull_up(i);               // Enable pull-up resistor
  }

  setNEOBusDir(mREAD);             // INPUT

  gpio_init(mRW);                  // Always init pins first
  gpio_set_dir(mRW, GPIO_OUT);     // Set as output
  setmRW(mHIGH);

  gpio_init(pDebug);               // Always init pins first
  gpio_set_dir(pDebug, GPIO_OUT);  // Set as output
  setDebug(mHIGH);
}
