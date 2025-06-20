// 
// 
// 
#include <Arduino.h>
#include "hardware/pwm.h"

#include "config.h"
#include "p6502.h"
#include "pins.h"
#include "control.h"

// desired frequency in Hz
uint32_t gClockFrequency = DEFAULT_6502_CLOCK;  // 1 MHz default

uint8_t gBusState;   // will be init in setup6502
uint8_t gCPUState;   // will be init in setup6502

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
}

/// <summary>
/// 
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void set6502RDY(const uint8_t vHL) {
  gpio_put(p6502RDY, vHL);
}

//
uint8_t gDir6502RW = mINPUT;

/// <summary>
/// set the direction of 6502 RW bus pin
/// </summary>
/// <param name="vDir"></param>
void dir6502RW(const uint8_t vDir) {
    switch (vDir) {
    case mINPUT:                            // always allowed
      if (gDir6502RW != mINPUT) {
        gpio_init(p6502RW);                 // Always init pins first
        gpio_set_dir(p6502RW, GPIO_IN);     // Set as input
        gpio_pull_up(p6502RW);              // Enable pull-up resistor
        gDir6502RW = mINPUT;
      }
      break;
    
    case mOUTPUT:
//      if (gBusState == eDISABLED) {          // only allowed when bus is disabled
        if (gDir6502RW != mOUTPUT) {
          gpio_init(p6502RW);                // Always init pins first
          gpio_set_dir(p6502RW, GPIO_OUT);   // Set as output
          set6502Reset(mHIGH);               // read
          gDir6502RW = mOUTPUT;
        }
//      }
//      else
//        Serial.printf("*E: dir6502RW: bus NOT disabled");
      break;
    
    default:
      Serial.printf("*E: dir6502RW: invalid direction\n");
      break;
    }
}

/// <summary>
/// 
/// </summary>
/// <param name="vHL"></param>
void set6502RW(const uint8_t vHL) {
  if (gDir6502RW == mOUTPUT)
    gpio_put(p6502RW, vHL);
  else
    Serial.printf("*E: set6502RW: RW pin NOT output\n");
}

/// <summary>
/// set 6502 PHI2 clock signal
/// </summary>
/// <param name="freq"></param>
void set6502Clock(const uint32_t target_freq) {
  const uint32_t pwm_clk = 125000000L;

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
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////


/// <summary>
/// set the state of the 6502 processor
/// </summary>
bool set6502State(const uint8_t cpuState, const uint8_t busState) {
  switch (cpuState) {
  case eRESET:
    set6502Reset(mLOW);      // reset

    switch (busState) {
    case eDISABLED:
      setControlMode(mRPI);                // RPI can take control
      set6502BE(mLOW);
      dir6502RW(mOUTPUT);                  // force RW-pin as output
      break;
    
    case eENABLED:
      setControlMode(mCPU);                // RPI cannot take control
      dir6502RW(mINPUT);                   // force RW-pin as input
      set6502BE(mHIGH);
      break;
    
    default:
      Serial.printf("*E: set6502State: RESET in unknown bus state\n");
      return false;
      break;
    }
    break;

  case eRUN:
    setControlMode(mCPU);                // RPI can not take control
    dir6502RW(mINPUT);                   // force RW-pin as input
    set6502BE(mHIGH);
    set6502RDY(mHIGH);
    set6502Reset(mHIGH);
    break;

  case eHALTED:
    gCPUState = cpuState;
    set6502RDY(mLOW);                      // halted

    switch (busState) {
    case eDISABLED:
      setControlMode(mRPI);                // RPI can take control
      set6502BE(mLOW);
      dir6502RW(mOUTPUT);                  // force RW-pin as output
      break;

    case eENABLED:
      setControlMode(mCPU);                // RPI cannot take control
      dir6502RW(mINPUT);                   // force RW-pin as input
      set6502BE(mHIGH);
      break;
    
    default:
      Serial.printf("*E: set6502State: HALTED in unknown bus state\n");
      return false;
      break;
    }
    break;

  default:
    Serial.printf("*E: set6502State: invalid state\n");
    return false;
    break;
  }

  gCPUState = cpuState;
  gBusState = busState;
  return true;
}

//
static const char lTxtCPUState[3][8] = {
  "RESET",
  "RUN",
  "HALTED"
};

//
static const char lTxtBusState[2][9] = {
  "DISABLED", 
  "ENABLED"
};

//
static const char lTxtControlState[2][4] = {
  "RPI",
  "CPU"
};

/// <summary>
/// show states of 6502
/// </summary>
void state6502() {
  Serial.printf("*I: CPU: %s\tBus: %s\tControl: %s\n", lTxtCPUState[gCPUState], lTxtBusState[gBusState], lTxtControlState[getControlMode()]);
}

/// <summary>
/// 
/// </summary>
void init6502() {
  // turn on PHI2 clock
  set6502Clock(DEFAULT_6502_CLOCK);   // 1MHz

  state6502();
}

/// <summary>
/// setup the pins for the 6502 and in reset/disabled state
/// </summary>
void setup6502() {
  gpio_init(p6502RESET);              // Always init
  gpio_set_dir(p6502RESET, GPIO_OUT); // Set as output
  set6502Reset(mLOW);                 // reset
  gCPUState = eRESET;

  gpio_init(p6502BE);                 // Always init
  gpio_set_dir(p6502BE, GPIO_OUT);    // Set as output
  set6502BE(mLOW);                    // disable
  gBusState = eDISABLED;

  gpio_init(p6502RDY);                // Always init
  gpio_set_dir(p6502RDY, GPIO_OUT);   // Set as output
  set6502RDY(mLOW);                   // halted

  gpio_init(p6502RW);                 // Always init
  gpio_set_dir(p6502RW, GPIO_OUT);    // Set as output
  gDir6502RW = mOUTPUT;
  set6502RW(mREAD);                   // read

  gpio_init(p6502PHI2);               // Always init
  gpio_set_function(p6502PHI2, GPIO_FUNC_PWM); // PWM output
}
