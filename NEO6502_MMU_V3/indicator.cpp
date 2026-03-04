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

#include "Arduino.h"
#include "pico/stdlib.h"
#include "hardware/pwm.h"

#include "indicator.h"
#include "p6502.h"

constexpr uint LED_PIN = LED_BUILTIN;       // the pin number of the LED used for the indicator, this should be defined in the board definition header and can be changed if necessary
constexpr uint16_t PWM_TOP = 100;            // the maximum value for the PWM, this determines the resolution of the brightness control for the LED.

/// <summary>
/// Led modes for the indicator. Off, on, blink and fade. Blink and fade will use the parameters defined in the LedProfile structure to determine the behavior of the LED.
/// </summary>
enum LedMode {
  LED_OFF,
  LED_ON,
  LED_BLINK,
  LED_FADE
};

/// <summary>
/// Led profile structure defining the behavior of the LED for each state of the 6502. It includes the mode (off, on, blink, fade) and parameters for fading and blinking.
/// </summary>
struct LedProfile {
  LedMode mode;
  uint16_t fadeUp;
  uint16_t fadeDown;
  uint16_t onTime;
  uint16_t offTime;
};

/// <summary>
/// Led profiles for each state of the 6502. The order must match the order of the states in p6502.h
/// </summary>
static const LedProfile ledProfiles[] = {
  // sBOOT
  { LED_BLINK, 0, 0, 50, 50 },         // fast blink

  // sRESET
  { LED_BLINK, 0, 0, 50, 50 },         // fast blink

  // sHALTED
  { LED_BLINK, 0, 0, 20, 200 },        // short pulse

  // sRUNNING
  { LED_FADE, 500, 500, 0, 0 },        // slow breathing

  // sREAD
  { LED_ON, 0, 0, 0, 0 },              // solid on

  // sRPI
  { LED_OFF, 0, 0, 0, 0 }              // off
};

static uint pwmSlice;                 // the PWM slice number associated with the LED pin
static uint32_t lastTime = 0;         // the last time the LED state was updated, used for timing the blinking and fading
static uint16_t brightness = 0;       // the current brightness of the LED, used for fading
static bool direction = true;         // the direction of the fade, true for fading up and false for fading down
static bool blinkState = false;       // the current state of the blink, true for on and false for off

/// <summary>
/// set the brightness of the LED. The value should be between 0 and PWM_TOP. 
/// 0 is off and PWM_TOP is fully on.
/// </summary>
/// <param name="v"></param>
inline void setBrightness(uint16_t v) {
  pwm_set_gpio_level(LED_PIN, v);
}

/// <summary>
/// update the indicator based on the current state of the 6502. This function should be called in the main loop of the program to ensure that the indicator is updated regularly. 
/// The behavior of the indicator will depend on the current state of the 6502 and the corresponding LedProfile defined in the ledProfiles array.
/// </summary>
void updateIndicator() {
  static uint8_t lastState = 0xFF;

  uint32_t now = to_ms_since_boot(get_absolute_time());

  uint8_t current = get6502State();

  if (current != lastState) {
    brightness = 0;
    direction = true;
    blinkState = false;
    lastTime = now;
    lastState = current;
  }

  const LedProfile& p = ledProfiles[current];

  switch (p.mode) {
  case LED_OFF:
    setBrightness(0);
    break;

  case LED_ON:
    setBrightness(PWM_TOP);
    break;

  case LED_BLINK:
    if (now - lastTime > (blinkState ? p.onTime : p.offTime)) {
      blinkState = !blinkState;
      setBrightness(blinkState ? PWM_TOP : 0);
      lastTime = now;
    }
    break;

  case LED_FADE:
  {  // just in case, to limit the scope of variables
    uint16_t interval = direction ? p.fadeUp : p.fadeDown;

    if (interval == 0)
      interval = 1;

    uint32_t step = interval / PWM_TOP;
    if (step == 0) step = 1;

    if (now - lastTime > step) {
      if (direction)
        brightness++;
      else
        brightness--;

      if (brightness >= PWM_TOP)
        direction = false;

      if (brightness == 0)
        direction = true;

      setBrightness(brightness);

      lastTime = now;
    }
    break;
  }
  }
}

/// <summary>
/// initialize the indicator. This function should be called in the setup function of the program to ensure that the indicator is properly initialized before it is used. 
/// It sets up the PWM for the LED pin and configures the PWM slice for use with the indicator.
/// </summary>
void initIndicator()
{
  gpio_set_function(LED_PIN, GPIO_FUNC_PWM);

  pwmSlice = pwm_gpio_to_slice_num(LED_PIN);

  pwm_set_wrap(pwmSlice, PWM_TOP);
  pwm_set_enabled(pwmSlice, true);
}
