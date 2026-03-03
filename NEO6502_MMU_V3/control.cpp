/*
This software is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This software is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

*/
#include <Arduino.h>
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
static uint8_t gBusDir = 99;  // invalid :-)

/// <summary>
/// get mode of control
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
/// set NEObus direction to input or output
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
/// reset NEObus
/// </summary>
void resetNEOBus() {
  setNEOBusDir(mREAD);
}

/// <summary>
/// setup control pins: mRW for read/write, pDebug for debug output, and pin 40..47 for NEObus data bus
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
  MRWPin::high();                  // default to read mode

  gpio_init(pDebug);               // Always init pins first
  gpio_set_dir(pDebug, GPIO_OUT);  // Set as output
  DebugPin::high();
}
