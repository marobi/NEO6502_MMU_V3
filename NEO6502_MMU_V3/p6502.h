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
#pragma once

typedef enum {
  mWRITE = 0,
  mREAD
} rw_t;

typedef enum {
  mOUTPUT = 0,
  mINPUT,
  mUNKNOWN
} direction_t;

// state of 6502 cpu
typedef enum {
 eRESET = 0,
 eRUN,
 eHALTED
} cpustate_t;

// state of 6502 bus control
typedef enum {
  eDISABLED = 0,
  eENABLED
} busstate_t;

typedef enum {
  eOFF = 0,
  eON
} clockstate_t;

typedef enum {
//                    CTRL   STATE   PHI2   BUS   DIR
  sSTARTUP = 0,
  sBOOT    ,       // RPI    RESET   OFF    DIS   IN
  sRESET,          // CPU    RESET   ON     ENA   IN
  sHALTED,         // CPU    HALTED  OFF    ENA   IN
  sRUNNING,        // CPU    RUN     ON     ENA   IN
  sREAD,           // RPI    HALTED  OFF    ENA   IN
  sRPI             // RPI    HALTED  OFF    DIS   OUT
} sysstate_t;

void set6502RW(const rw_t);

rw_t get6502RW();

void set6502Clockfrequency(const uint32_t);

void set6502Clock();

bool halt6502clock(const bool);

void singleCycle6502(const uint8_t, const bool);

void singleStep6502(const bool);

sysstate_t get6502State();

bool set6502State(const sysstate_t);

void show6502State();

void init6502();

void setup6502();
