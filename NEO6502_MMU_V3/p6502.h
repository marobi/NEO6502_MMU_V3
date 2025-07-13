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

enum clockState {
  eOFF = 0,
  eON,
};

enum sysState {
//                    CTRL   STATE   PHI2   BUS   DIR
  sBOOT = 0,       // RPI    RESET   OFF    DIS   IN
  sRESET,          // CPU    RESET   ON     ENA   IN
  sHALTED,         // CPU    HALTED  OFF    ENA   IN
  sRUNNING,        // CPU    RUN     ON     ENA   IN
  sREAD,           // RPI    HALTED  OFF    ENA   IN
  sRPI             // RPI    HALTED  OFF    DIS   OUT
};

#define eKEEP   99

void set6502RW(const uint8_t);

uint8_t get6502RW();

uint8_t getClockState();

void set6502Clock(const uint32_t);

bool reset6502Clock(const bool);

void singleStep6502(const uint8_t, const bool);

uint8_t get6502State();

bool set6502State(const uint8_t);

void state6502();

void init6502();

void setup6502();
