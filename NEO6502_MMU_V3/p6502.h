// p6502.h

#pragma once

// state of 6502 cpu
enum cpuState {
 eRESET = 0,
 eRUN,
 eHALTED
};

// state of 6502 bus control
enum busState {
  eDISABLED = 0,
  eENABLED
};

/*
                    DISABLED  ENABLED
    eRESET             X        X
    eRUN                        X
    eHALTED            X        X

                          RESET RDY BE   RW
    eRESET    DISABLED      0    0   0   rpi
              ENABLED       0    1   1   cpu
    eRUN      ENABLED       1    1   1   cpu
    eHALTED   DISABLED      1    0   0   rpi
              ENABLED       1    0   1   cpu
*/

void set6502RW(const uint8_t);

void set6502Clock(const uint32_t);

bool set6502State(const uint8_t, const uint8_t);

void state6502();

void init6502();

void setup6502();

