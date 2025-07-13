// 
// 
// 
#include <hardware/pwm.h>
#include <arduino.h>

#include "config.h"
#include "control.h"
#include "neobus.h"
#include "p6502.h"
#include "pins.h"

//
static const char lTxtSystemState[6][7] = {
  "BOOT ",
  "RESET",
  "HALT ",
  "RUN  ",
  "READ ",
  "RPI  "
};

static const char lTxtClockState[3][5] = {
  "OFF",
  "ON "
};

//
static const char lTxtRWState[2][7] = {
  "OUT",
  "IN "
};

//
static const char lTxtBusState[2][9] = {
  "DIS",
  "ENA",
};

//
static const char lTxtControlState[2][4] = {
  "RPI",
  "CPU"
};

// desired frequency in Hz
static uint32_t gClockFrequency = DEFAULT_6502_CLOCK;  // 4 MHz default

static uint8_t gSysState;       // will be init in setup6502
static uint8_t gBusState;       // will be init in setup6502
static uint8_t gDir6502RW = 99;
static uint8_t gClockState;     // will be init in setup6502

/// <summary>
/// control 6502 RESET
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void set6502Reset(const uint8_t vHL) {
  gpio_put(p6502RESET, vHL);
}

/// <summary>
/// control 6502 BE
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void set6502BE(const uint8_t vHL) {
  gpio_put(p6502BE, vHL);

  gBusState = vHL;
}

/// <summary>
/// 
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void set6502RDY(const uint8_t vHL) {
  gpio_put(p6502RDY, vHL);

  DELAY_FACTOR_SHORT();
}

/// <summary>
/// 
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void set6502PHI2(const uint8_t vHL) {
  gpio_put(p6502PHI2, vHL);
}

/// <summary>
/// 
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void set6502IRQ(const uint8_t vHL) {
  gpio_put(p6502IRQ, vHL);
}

/// <summary>
/// set RW pin state
/// </summary>
/// <param name="vHL"></param>
void set6502RW(const uint8_t vHL) {
  if (gDir6502RW == mOUTPUT)
    gpio_put(p6502RW, vHL);
  else
    Serial.printf("*E: set6502RW: not output [%s]\n", lTxtSystemState[gSysState]);
}

/// <summary>
/// get RW pin state
/// </summary>
/// <returns></returns>
uint8_t get6502RW() {
  if (gDir6502RW == mINPUT)
    return gpio_get(p6502RW);
  else
    Serial.printf("*E: get6502RW: not input [%s]\n", lTxtSystemState[gSysState]);

  return mREAD;
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint8_t getClockState() {
  return gClockState;
}

/// <summary>
/// set the direction of 6502 RW bus pin
/// </summary>
/// <param name="vDir"></param>
void dir6502RW(const uint8_t vDir) {
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
      set6502RW(mHIGH);                  // read
      gpio_set_dir(p6502RW, GPIO_OUT);   // Set as output
      set6502RW(mHIGH);                  // for sure
    }
    break;

  default:
    Serial.printf("*E: dir6502RW: invalid direction\n");
    break;
  }
}

/// <summary>
/// set 6502 PHI2 clock signal
/// </summary>
/// <param name="freq"></param>
void set6502Clock(const uint32_t target_freq) {
  const uint32_t pwm_clk = 125000000L;

  gpio_set_function(p6502PHI2, GPIO_FUNC_PWM); // PWM output

  uint slice_num = pwm_gpio_to_slice_num(p6502PHI2);
  uint channel = pwm_gpio_to_channel(p6502PHI2);

  // PWM toggles once per cycle => need 1/2x the desired frequency
  uint32_t pwm_freq = target_freq / 2;

  gClockFrequency = target_freq;

  float divider = (float)pwm_clk / (float)pwm_freq / 65536.0f;
  if (divider < 1.0f) divider = 1.0f;

  uint32_t divider16 = (uint32_t)(divider * 16.0f + 0.5f);
  uint32_t wrap = ((pwm_clk * 16) / divider16 / pwm_freq) - 1;

  pwm_set_clkdiv_int_frac(slice_num, divider16 / 16, divider16 & 0xF);
  pwm_set_wrap(slice_num, wrap);
  pwm_set_chan_level(slice_num, channel, wrap / 2);  // 50% duty cycle
  pwm_set_enabled(slice_num, true);

  gClockState = eON;
}

/// <summary>
/// initialise the clock running
/// </summary>
void init6502Clock() {
  if (gClockState != eON)
    set6502Clock(DEFAULT_6502_CLOCK);
}

//// <summary>
/// clock step in
/// </summary>
inline __attribute__((always_inline))
void _ss6502ClockIn() {
  set6502PHI2(mLOW);        // to be sure

  DELAY_FACTOR_SHORT();

  set6502PHI2(mHIGH);

  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
}

/// <summary>
/// clock step out
/// </summary>
inline __attribute__((always_inline))
void _ss6502ClockOut() {
  set6502PHI2(mLOW);

  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
}

// <summary>
/// stop the clock in low state optional in a read cycle
/// </summary>
/// <param name="vToRead"></param>
/// <returns></returns>
bool reset6502Clock(const bool vToRead) {
  if (gClockState == eOFF) return true;

  uint8_t lTry = 0;

  uint slice_num = pwm_gpio_to_slice_num(p6502PHI2);

  pwm_set_enabled(slice_num, false);

  DELAY_FACTOR_SHORT();

  gpio_set_function(p6502PHI2, GPIO_FUNC_SIO); // GPIO output
  gpio_init(p6502PHI2);
  gpio_set_dir(p6502PHI2, GPIO_OUT);

  set6502PHI2(mLOW);

  gClockState = eOFF;

  if (vToRead) {
    while ((get6502RW() == mWRITE) && (lTry++ < 4)) {   // continue till in read cycle
      _ss6502ClockIn();
      _ss6502ClockOut();
    }

    if (lTry >= 4) {
      Serial.println("*E: reset6502Clock: STOPPED but in unknown clock state");
      return false;
    }
  }

  return true;
}

/// <summary>
/// single step the 6502 (cycle)
/// </summary>
/// <param name="vSteps"></param>
/// <param name="vDisplay"></param>
void singleStep6502(const uint8_t vSteps, const bool vDisplay) {
  uint8_t lState = get6502State();
  set6502State(sREAD);

  if (vSteps == 0) {
    if (vDisplay) {
      Serial.printf("%02d:\t%04X: %02X %1d\n", 0, readCPUAddress(), read6502Data(), get6502RW());
    }
    return;
  }
  for (uint8_t s = 0; s < vSteps; s++) {
    _ss6502ClockIn();

    if (vDisplay) {
      Serial.printf("s%02d:\t%04X: %02X %1d\n", s, readCPUAddress(), read6502Data(), get6502RW());
    }
    _ss6502ClockOut();
  }

  set6502State(lState);  // restore state
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// <summary>
/// get neo6502_mmu state
/// </summary>
/// <returns></returns>
uint8_t get6502State() {
  return gSysState;
}

/// <summary>
/// set neo6502_mmu system in a state
/// </summary>
/// <param name="vSysState"></param>
/// <returns></returns>
bool set6502State(const uint8_t vSysState) {
  //  Serial.printf("*D: set6502State: %s --> %s\n", lTxtSystemState[gSysState], lTxtSystemState[vSysState]);

  if (vSysState == gSysState) return true;

  switch (vSysState) {
  case sBOOT:  // system in boot mode
    set6502Reset(mLOW);
    reset6502Clock(false);
    set6502RDY(mLOW);
    set6502BE(mLOW);
    dir6502RW(mOUTPUT);
    setControlMode(mRPI);
    break;

  case sRESET:  // cpu helt reset
    set6502Reset(mLOW);
    init6502Clock();
    set6502RDY(mLOW);
    set6502BE(mHIGH);
    dir6502RW(mINPUT);
    setControlMode(mCPU);
    break;

  case sHALTED: // cpu stopped
    set6502Reset(mHIGH);
    reset6502Clock(true);
    set6502RDY(mLOW);
    set6502BE(mHIGH);
    dir6502RW(mINPUT);
    setControlMode(mCPU);
    break;

  case sRUNNING: // cpu running free
    set6502Reset(mHIGH);
    set6502BE(mHIGH);
    dir6502RW(mINPUT);
    init6502Clock();
    set6502RDY(mHIGH);
    setControlMode(mCPU);
    break;

  case sREAD:  // SS mode
    set6502Reset(mHIGH);
    reset6502Clock(true);
    set6502RDY(mHIGH);
    set6502BE(mHIGH);
    dir6502RW(mINPUT);
    setControlMode(mRPI);
    break;

  case sRPI: // rpi control mode, cpu halted
    set6502Reset(mHIGH);
    reset6502Clock(true);
    set6502RDY(mLOW);
    set6502BE(mLOW);
    dir6502RW(mOUTPUT);
    setControlMode(mRPI);
    break;

  default:
    Serial.println("*E: set6502State: unknown state specified");
    return false;
    break;
  }

  gSysState = vSysState;
  return true;
}

/// <summary>
/// show substates of 6502
/// </summary>
void state6502() {
  Serial.printf("*I: SYS: %s\tBUS: %s\tCTL: %s\tRW: %s\tCLK: %s\n",
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
  gSysState = 99;

  set6502State(sBOOT);

  state6502();
}

/// <summary>
/// setup the pins for the 6502 and in BOOTable state
/// </summary>
void setup6502() {
  gpio_init(p6502RESET);              // Always init
  gpio_set_dir(p6502RESET, GPIO_OUT); // Set as output
  set6502Reset(mLOW);                 // reset

  gpio_init(p6502BE);                 // Always init
  gpio_set_dir(p6502BE, GPIO_OUT);    // Set as output
  set6502BE(mLOW);                    // disabled
  gBusState = eDISABLED;

  gpio_init(p6502RDY);                // Always init
  gpio_set_dir(p6502RDY, GPIO_OUT);   // Set as output
  set6502RDY(mLOW);                   // halted

  gpio_init(p6502RW);                 // Always init
  gpio_set_dir(p6502RW, GPIO_OUT);    // Set as output
  gDir6502RW = mOUTPUT;
  set6502RW(mREAD);                   // read

  gpio_init(p6502IRQ);                // Always init
  set6502IRQ(mHIGH);                  // no IRQ
  gpio_set_dir(p6502IRQ, GPIO_OUT);   // Set as output
  set6502IRQ(mHIGH);                  // no IRQ

  gpio_init(p6502PHI2);               // Always init
  gpio_set_function(p6502PHI2, GPIO_FUNC_PWM); // PWM output
  gClockState = eOFF;
}
