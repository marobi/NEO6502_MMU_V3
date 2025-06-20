// 
// 
// 
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"

#include "pins.h"
#include "control.h"
#include "mmu.h"
#include "p6502.h"

// maintains control
static uint8_t gControlMode = mRPI;

// maintains databus direction
static uint8_t gBusDir = 2;  // invalid :-)

/// <summary>
/// 
/// </summary>
/// <param name="vRW"></param>
void setDebug(const uint8_t vRW) {
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
void setmRW(const uint8_t vRW) {
  gpio_put(mRW, vRW);
  set6502RW(vRW);
}

/// <summary>
/// set GPIO as input/output
/// </summary>
/// <param name="lDirection"></param>
inline __attribute__((always_inline))
void  setDataBusDir(const uint8_t lDirection) {
  if (lDirection != gBusDir) {
    switch (lDirection) {
    case mWRITE:  
      gpio_set_dir_masked64(DATA_BUS_MASK, DATA_BUS_MASK);
//      for (uint8_t i = (40u); i <= (47u); i++) {
//        gpio_set_dir(i, GPIO_OUT);   // Set as output
//      }
      break;

    case mREAD:   
      gpio_set_dir_masked64(DATA_BUS_MASK, (uint64_t)0ULL);
//      for (uint8_t i = (40u); i <= (47u); i++) {
//        gpio_set_dir(i, GPIO_IN);   // Set as input
//      }
      break;
    }

    gBusDir = lDirection;
  }
}

/// <summary>
/// read a byte from NEObus
/// </summary>
/// <returns></returns>
uint8_t readDataBus() {
  setDataBusDir(mREAD);     // input

  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();

  uint32_t lData = ((uint32_t)sio_hw->gpio_hi_in) >> 8u;

  return lData & 0xFF;
}

/// <summary>
/// write a byte to the NEObus
/// </summary>
/// <param name="vData"></param>
void writeDataBus(const uint8_t vData) {
  setDataBusDir(mWRITE);

  gpio_put_masked64(DATA_BUS_MASK, ((uint64_t)vData << 40u));

  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
}

/// <summary>
/// reset NOEObus
/// </summary>
void resetDataBus() {
  DELAY_FACTOR_SHORT();

  setDataBusDir(mREAD);
}

/// <summary>
/// 
/// </summary>
void setupControl() {
  // NEO databus init
  for (uint8_t  i = (40u); i <= (47u); i++) {
    gpio_init(i);               // Always init pins first
    gpio_set_dir(i, GPIO_IN);   // Set as input
    gpio_pull_up(i);            // Enable pull-up resistor
  }

  setDataBusDir(mREAD);         // INPUT

  gpio_init(mRW);               // Always init pins first
  gpio_set_dir(mRW, GPIO_OUT);   // Set as output
  setmRW(mHIGH);

  gpio_init(pDebug);               // Always init pins first
  gpio_set_dir(pDebug, GPIO_OUT);   // Set as output
  setDebug(mHIGH);
}
