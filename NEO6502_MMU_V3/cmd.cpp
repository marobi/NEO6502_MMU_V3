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
#include "mmu.h"
#include "cmd.h"
#include "neobus.h"

/// <summary>
/// readm a byte from 6502 slot-interface
/// </summary>
/// <param name="vSlot"></param>
/// <param name="vData"></param>
static uint8_t readCmdSlot(const uint8_t vSlot) {
  uint8_t lData = snoop_read6502MemoryLoc(CMD_SLOT_BASE + vSlot);

  return lData;
}

/// <summary>
/// write a byte to 6502 slot-interface
/// </summary>
/// <param name="vSlot"></param>
/// <param name="vData"></param>
static void writeCmdSlot(const uint8_t vSlot, uint8_t vData) {

  snoop_write6502MemoryLoc(CMD_SLOT_BASE + vSlot, vData);
}


// --------------------------------------------------------------

static uint16_t inCount = 0;                             // clutch because for unknown reason we can miss mmuInt interrupts

/// <summary>
/// get a 
/// </summary>
/// <returns></returns>
bool getCommand6502(uint8_t &vCmd, uint8_t& vParam) {
  inCount++;

  if (triggerMMUIO() || !( inCount % 1000L)) {          // got mmuInt interrupt or force checking
    uint8_t cmd = readCmdSlot(CMD_SLOT_CMD);
    if (cmd != CMD6502_NONE) {
      vCmd = cmd;
      vParam = readCmdSlot(CMD_SLOT_PARAM);
      ackMMUIO();
      return true;
    }
  }

  vCmd = CMD6502_NONE;
  return false;
}

/// <summary>
/// ACK command: write 0x00 to CMD_SLOT_CMD to ACK command received
/// </summary>
void ackCommand6502() {
  writeCmdSlot(CMD_SLOT_CMD, 0x00);
}

/// <summary>
/// init cmd slots: set to 0x00
/// </summary>
void initCmdInterface() {
  uint8_t ldata[4] = { 0x00, 0x00, 0x00, 0x00 };

  snoop_write6502Memory(CMD_SLOT_BASE, 4, ldata);
}
