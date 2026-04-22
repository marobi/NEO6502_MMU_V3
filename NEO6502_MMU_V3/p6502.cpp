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

#include <hardware/pwm.h>
#include <arduino.h>

#include "config.h"
#include "control.h"
#include "neobus.h"
#include "p6502.h"
#include "mmu.h"
#include "pins.h"

/// <summary>
/// system states
/// </summary>
static const char lTxtSystemState[7][6] = {
  "SUP",
  "BOOT",
  "RESET",
  "HALT",
  "RUN",
  "READ",
  "RPI"
};

/// <summary>
/// clockstates
/// </summary>
static const char lTxtClockState[2][4] = {
  "OFF",
  "ON"
};

/// <summary>
/// RW pi states
/// </summary>
static const char lTxtRWState[2][4] = {
  "OUT",
  "IN"
};

/// <summary>
/// cpu bus states
/// </summary>
static const char lTxtBusState[2][4] = {
  "DIS",
  "ENA",
};

/// <summary>
/// bus control processor states
/// </summary>
static const char lTxtControlState[2][4] = {
  "RPI",
  "CPU"
};

// desired frequency in Hz
static uint32_t     gClockFrequency = DEFAULT_6502_CLOCK;
static sysstate_t   gSysState;       // will be init in setup6502
static busstate_t   gBusState;       // will be init in setup6502
static direction_t  gDir6502RW = mUNKNOWN;
static clockstate_t gClockState;     // will be init in setup6502

/// <summary>
/// set RW pin state
/// </summary>
/// <param name="vHL"></param>
void set6502RW(const rw_t vHL) {
  if (gDir6502RW == mOUTPUT)
    RWPin::set((uint8_t)vHL);
  else
    Serial1.printf("*E: set6502RW: not output [%s]\n", lTxtSystemState[gSysState]);
}

/// <summary>
/// get RW pin state
/// </summary>
/// <returns></returns>
rw_t get6502RW() {
  if (gDir6502RW == mINPUT)
    return (rw_t)gpio_get(p6502RW);
  else
    Serial1.printf("*E: get6502RW: not input [%s]\n", lTxtSystemState[gSysState]);

  return mREAD;
}

/// <summary>
/// set the direction of 6502 RW bus pin
/// </summary>
/// <param name="vDir"></param>
static void dir6502RW(const direction_t vDir) {
  switch (vDir) {
  case mINPUT:                            // always allowed
    if (gDir6502RW != mINPUT) {
      gpio_pull_up(p6502RW);              // Enable pull-up resistor
      gpio_set_dir(p6502RW, GPIO_IN);     // Set as input
      gDir6502RW = mINPUT;
    }
    break;

  case mOUTPUT:
    if (gDir6502RW != mOUTPUT) {
      gDir6502RW = mOUTPUT;
      set6502RW(mREAD);                  // Read
      gpio_set_dir(p6502RW, GPIO_OUT);   // Set as output
      set6502RW(mREAD);                  // for sure
    }
    break;

  default:
    Serial1.printf("*E: dir6502RW: invalid direction\n");
    break;
  }
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint32_t get6502ClockFrequency() {
  return gClockFrequency;
}

/// <summary>
/// 
/// </summary>
/// <param name="freq"></param>
void set6502Clockfrequency(const uint32_t freq) {
  gClockFrequency = freq;
}

/// <summary>
/// set 6502 PHI2 clock signal
/// </summary>
/// <param name="freq"></param>
void set6502Clock() {
  const uint32_t pwm_clk = 125000000L;

  gpio_set_function(p6502PHI2, GPIO_FUNC_PWM); // PWM output

  uint slice_num = pwm_gpio_to_slice_num(p6502PHI2);
  uint channel = pwm_gpio_to_channel(p6502PHI2);

  // PWM toggles once per cycle => need 1/2x the desired frequency
  uint32_t pwm_freq = gClockFrequency / 2;

  float divider = (float)pwm_clk / (float)pwm_freq / 65536.0f;
  if (divider < 1.0f) divider = 1.0f;

  uint32_t divider16 = (uint32_t)(divider * 16.0f + 0.5f);
  uint32_t wrap = ((pwm_clk * 16) / divider16 / pwm_freq) - 1;

  pwm_set_clkdiv_int_frac(slice_num, divider16 / 16, divider16 & 0xF);
  pwm_set_wrap(slice_num, wrap);
  pwm_set_chan_level(slice_num, channel, wrap / 2);  // 50% duty cycle
//  pwm_set_chan_level(slice_num, channel, (wrap * 40) / 100);  // duty cycle
  pwm_set_enabled(slice_num, true);

  gClockState = eON;
}

//// <summary>
/// clock step in
/// </summary>
static void ss6502ClockStep() {
  PHI2Pin::low();

  delayNs<125>();

  PHI2Pin::high();

  delayNs<125>();
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
static bool advance6502clock() {
  uint8_t lTry = 0;

  if (gClockState == eON) {
    Serial1.println("*E: advance6502clock: clock is running");
    return false;
  }

  while ((get6502RW() == mWRITE) && (lTry++ < 8)) {   // continue till in read cycle
    ss6502ClockStep();                                // step
  }
  if (lTry >= 8) {
    Serial1.println("*E: advance6502clock: clock STOPPED in unknown state");
    return false;
  }
  return true;
}

/// <summary>
/// stop the clock in high state; optional in a read cycle
/// </summary>
/// <param name="vToRead"></param>
/// <returns></returns>
bool halt6502clock(const bool vToRead) {
  if (gClockState == eOFF) {
    return true;
  }

  uint slice_num = pwm_gpio_to_slice_num(p6502PHI2);

  pwm_set_enabled(slice_num, false);  // stop PWM

  delayNs<125>();

  gpio_set_function(p6502PHI2, GPIO_FUNC_SIO);          // GPIO output
  PHI2Pin::high();                                      // force high
  gpio_set_dir(p6502PHI2, GPIO_OUT);

  gClockState = eOFF;

  if (vToRead)
    return(advance6502clock());

  return true;
}

/// <summary>
/// single cycle the 6502
/// </summary>
/// <param name="vSteps"></param>
/// <param name="vDisplay"></param>
void singleCycle6502(const uint8_t vSteps, const bool vDisplay) {
  sysstate_t lState = get6502State();
  set6502State(sREAD);

  if (vSteps == 0) {
    if (vDisplay) {
      Serial1.printf("%02d:\t%04X: %02X %1s\n", 0, readCPUBusAddress(), read6502Data(), get6502RW() ? "R" : "W");
    }
    return;
  }
  for (uint8_t s = 0; s < vSteps; s++) {
    ss6502ClockStep();

    if (vDisplay) {
      Serial1.printf("s%02d:\t%04X: %02X %1s\n", s, readCPUBusAddress(), read6502Data(), get6502RW() ? "R" : "W");
    }
    delayNs<70>();
  }

  set6502State(lState);  // restore state
}

/// <summary>
/// SINGLE STEP CPU
/// </summary>
/// <param name="vDisplay"></param>
void singleStep6502(const bool vDisplay) {
  sysstate_t lState = get6502State();

  set6502State(sHALTED);
  set6502State(sREAD);

    do {
      ss6502ClockStep();

      if (gpio_get(p6502SYNC)) {
        if (vDisplay) {
          Serial1.printf("%04X: %02X\n", readCPUBusAddress(), read6502Data());
        }
      }
      delayMicroseconds(1);

    } while (!gpio_get(p6502SYNC));

  set6502State(lState);  // restore state
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// <summary>
/// get neo6502_mmu system state
/// </summary>
/// <returns></returns>
sysstate_t get6502State() {
  return gSysState;
}

/// <summary>
/// set neo6502_mmu system in a state
/// </summary>
/// <param name="vSysState"></param>
/// <returns></returns>
bool set6502State(const sysstate_t vSysState) {
  //Serial1.printf("*D: set6502State: %s --> %s\n", lTxtSystemState[gSysState], lTxtSystemState[vSysState]);

  if (vSysState == gSysState) 
    return true;

  switch (vSysState) {
  case sSTARTUP:
    Serial1.println("*E: set6502State: do not select!!\n");
    return false;
    break;

  case sBOOT:  // system in boot mode
    IRQPin::high();
    RESETPin::low();                    // reset
    halt6502clock(true);                // PHI2 = high
    RDYPin::low();
    BEPin::low();
    dir6502RW(mOUTPUT);
    setControlMode(mRPI);
    break;

  case sRESET:  // cpu helt reset
    IRQPin::high();
    DebugPin::high(); // TEMPORARY !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    RESETPin::low();
    set6502Clock();
    RDYPin::low();
    BEPin::high();
    dir6502RW(mINPUT);
    setControlMode(mCPU);

    setMMUContext(DEFAULT_CONTEXT);
    break;

  case sHALTED: // cpu stopped
    halt6502clock(true);      // PHI2 = high
    RDYPin::low();
    BEPin::high();
    dir6502RW(mINPUT);
    setControlMode(mCPU);
    break;

  case sRUNNING: // cpu running free
    dir6502RW(mINPUT);
    BEPin::high();
    set6502Clock();
    RDYPin::high();
    RESETPin::high();
    setControlMode(mCPU);
    break;

  case sREAD:  // SS mode
    RESETPin::high();
    halt6502clock(true);     // PHI2 = high
    RDYPin::high();
    BEPin::high();
    dir6502RW(mINPUT);
    setControlMode(mRPI);
    break;

  case sRPI: // rpi control mode, cpu halted
    halt6502clock(true);     // PHI2 = high
    RDYPin::low();
    BEPin::low();
    dir6502RW(mOUTPUT);
    setControlMode(mRPI);
    break;

  default:
    Serial1.println("*E: set6502State: unknown state specified");
    return false;
    break;
  }

  gSysState = vSysState;
  
  return true;
}

/// <summary>
/// show substates of 6502
/// </summary>
void show6502State() {
  Serial1.printf("*I: SYS: %s,  BUS: %s,  CTL: %s,  RW: %s,  CLK: %s\n",
    lTxtSystemState[gSysState],
    lTxtBusState[gBusState],
    lTxtControlState[getControlMode()],
    lTxtRWState[gDir6502RW],
    lTxtClockState[gClockState]);
}

/// <summary>
/// initialise the 6502 cpu
/// </summary>
void init6502() {
  set6502State(sBOOT);

  show6502State();
}

/// <summary>
/// setup the pins for the 6502 and in BOOTable state
/// </summary>
void setup6502() {
  gpio_init(p6502RESET);              // Always init
  gpio_set_dir(p6502RESET, GPIO_OUT); // Set as output
  RESETPin::low();                    // reset

  gpio_init(p6502BE);                 // Always init
  gpio_set_dir(p6502BE, GPIO_OUT);    // Set as output
  BEPin::low();                       // bus disabled
  gBusState = eDISABLED;

  gpio_init(p6502RDY);                // Always init
  gpio_set_dir(p6502RDY, GPIO_OUT);   // Set as output
  RDYPin::low();                      // halted

  gpio_init(p6502RW);                 // Always init
  gpio_set_dir(p6502RW, GPIO_OUT);    // Set as output
  gDir6502RW = mOUTPUT;
  set6502RW(mREAD);                   // read

  gpio_init(p6502SYNC);               // Always init
  gpio_set_dir(p6502SYNC, GPIO_IN);   // Set as input

  gpio_init(p6502IRQ);                // Always init
  gpio_set_dir(p6502IRQ, GPIO_OUT);   // Set as output
  IRQPin::high();                     // no IRQ

  gpio_init(p6502PHI2);               // Always init
  gpio_set_dir(p6502PHI2, GPIO_OUT);  // Set as output
  PHI2Pin::high();
  gClockState = eOFF;

  gSysState = sSTARTUP;               // startup state
}
