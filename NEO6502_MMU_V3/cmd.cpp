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
#include "input.h"

///-------------------------------------------------------------
/// CMD_LOT_BASE:
/// CMD_OUTCHAR: host -> write a char to this slot
/// CMD_INCHAR: 6502  -> read a char from this slot
/// CMD_COMMAND: host -> read a command code from this slot
/// 
#define CMD_SLOT_BASE    0xD000   // sync with memory.ini
#define CMD_SLOT_OUTCHAR 0        // write to 6502
#define CMD_SLOT_INCHAR  1        // read from 6502
#define CMD_SLOT_CMD     2
#define CMD_PARAM_BASE   (0x0100)

/// <summary>
/// init cmd slots: set to 0x00
/// </summary>
void initCmdInterface() {
  uint8_t ldata[3] = { 0x00, 0x00, 0x00 };

  snoop_write6502Memory(CMD_SLOT_BASE, 3, &ldata[0]);
  gMMUIOTrigger = false;

  inpInit();
}

/// <summary>
/// readm a char from 6502 slot-interface
/// </summary>
/// <param name="vSlot"></param>
/// <param name="vData"></param>
uint8_t readCmdSlot(const uint8_t vSlot) {
  uint8_t lData;
  snoop_read6502Memory(CMD_SLOT_BASE + vSlot, 1, &lData);

  return lData;
}

/// <summary>
/// write a char to 6502 slot-interface
/// </summary>
/// <param name="vSlot"></param>
/// <param name="vData"></param>
void writeCmdSlot(const uint8_t vSlot, uint8_t vData) {
  uint8_t lData = vData;
  snoop_write6502Memory(CMD_SLOT_BASE + vSlot, 1, &lData);
}

// --------------------------------------------------------------

static uint32_t inCount = 0;                           // TODO ugly clutch because for unknown reason we mis mmuInt interrupts

/// <summary>
/// read a char from 6502
/// </summary>
/// <param name="vChar"></param>
/// <returns></returns>
uint8_t inChar6502() {
  uint8_t lChar = 0x00;

  inCount++;

  if (gMMUIOTrigger || (! (inCount % 100L))) {        // got mmuInt interrupt or force checking
//    if ((!gMMUIOTrigger) && (!(inCount % 100L)))
//      DebugPin::low();

    lChar = readCmdSlot(CMD_SLOT_INCHAR);
    if (lChar != 0x00) {
      writeCmdSlot(CMD_SLOT_INCHAR, 0x00);            // ACK
      gMMUIOTrigger = false;                          // reset MMU IO trigger
    }
//    DebugPin::high();
  }

  return lChar;
}

/// <summary>
/// OutcharAvailable: check if 6502 is ready to receive char
/// </summary>
/// <returns></returns>
bool outCharAvailable6502() {
  uint8_t lChar = readCmdSlot(CMD_SLOT_OUTCHAR);
  return (lChar == 0);
}

/// <summary>
/// write a char to 6502 non blocking, 
/// return false if last char is not ACK (0x00)
/// </summary>
/// <param name="vChar"></param>
bool outChar6502(const uint8_t vChar) {
  if (outCharAvailable6502()) {
    writeCmdSlot(CMD_SLOT_OUTCHAR, vChar);
    return true;
  }

  return false;
}

/// <summary>
/// write a char to 6502 blocking, 
/// wait for ACK (0x00) before write
/// </summary>
/// <param name="vChar"></param>
void  outCharBlocking6502(const uint8_t vChar) {
  while (! outCharAvailable6502()) {
    delay(5);                        // TODO ulgh
  }

  writeCmdSlot(CMD_SLOT_OUTCHAR, vChar);
}

/// <summary>
/// get command from 6502: read command code from 6502 slot-interface if MMU IO trigger is set, otherwise return 0x00,
/// </summary>
/// <returns></returns>
uint8_t getCommand6502() {
  uint8_t lCmd = 0x00;

  if (gMMUIOTrigger) {
    lCmd = readCmdSlot(CMD_SLOT_CMD);
  }

  return lCmd;
}

/// <summary>
/// ACK command: write 0x00 to CMD_SLOT_CMD to ACK command received,
/// </summary>
void ackCommand6502() {
  gMMUIOTrigger = false;  // reset MMU IO trigger

  writeCmdSlot(CMD_SLOT_CMD, 0x00);
}

/// <summary>
/// Read command params: read command params from 6502 memory,
/// </summary>
/// <returns></returns>
bool readCommandParams(uint8_t vNumParams, uint8_t vParamlist[]) {
  snoop_read6502Memory(CMD_PARAM_BASE, vNumParams, vParamlist);

  return true;
}
