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
#include <arduino.h>
#include "config.h"
#include "ram.h"
#include "control.h"
#include "mmu.h"
#include "neobus.h"

/// <summary>
/// dump 16 bytes of memory
/// </summary>
/// <param name="vAddress"></param>
void dump16(const uint16_t vAddress) {
  uint8_t dat;

  Serial1.printf("%04X:", vAddress);
  for (uint8_t m = 0; m < 8; m++) {
    dat = snoop_read6502MemoryLoc(vAddress + m);
    Serial1.printf(" %02X", dat);
  }
  Serial1.printf(" ");
  for (uint8_t m = 8; m < 16; m++) {
    dat = snoop_read6502MemoryLoc(vAddress + m);
    Serial1.printf(" %02X", dat);
  }

  Serial1.println();
}

/// <summary>
/// dump memory
/// </summary>
/// <param name="vStartAddress"></param>
/// <param name="vEndAddress"></param>
void dumpMemory(const uint16_t vStartAddress, const uint16_t vEndAddress) {
  for (uint16_t ad = 0; ad < ((vEndAddress - vStartAddress) / 16) + 1; ad++) {
    dump16(vStartAddress + (ad * 16));
  }

  Serial1.println();
}

/// <summary>
/// fill memory with a value. 
/// You better not have your CPU running :-)
/// BTW current context
/// </summary>
void fillMemory(const uint8_t vVal) {
  for (int m = 0; m < 65536; m++) {
    write6502Memory(m, vVal);
  }
}