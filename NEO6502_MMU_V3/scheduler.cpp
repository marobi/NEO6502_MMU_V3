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
#include "scheduler.h"
#include "delay.h"
#include "neobus.h"
#include "mmu.h"
#include "p6502.h"
#include "cmd.h"

//
static bool statusContext[8] = {
  true,
  false,
  false,
  false,
  false,
  false,
  false,
  false
};

/// <summary>
/// TEMP routine
/// </summary>
/// <param name="str"></param>
static void schedHelp(const char* str) {
  uint8_t regs[7];

  snoop_read6502Memory(0x0228, 7, regs);

  Serial1.printf("%sPC=%02X%02X SR=%02X A=%02X X=%02X Y=%02X SP=%02X\n", str, regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6]);
}

/// <summary>
/// 
/// </summary>
/// <param name="vContext"></param>
bool schedSwitchcontext(const uint8_t vContext, const bool vForce) {
  uint16_t trial;
  uint8_t m;

  Serial1.printf("*I: SWITCH: CTX%1X -> CTX%1X %s\n", getMMUContext(), vContext, (vForce) ? "RESET" : "RUN");

  disableMMUInterrupt();

  writeCmdSlot(CMD_SLOT_SYNC, 0x01);

  IRQPin::low();                             // gen IRQ

  trial = 500;
  while (!triggerMMUIO() && (trial != 0)) {  // delay for 6502 to respond
    delayNs<50>();
    trial--;
  }

  trial = 50;
  do {
    m = readCmdSlot(CMD_SLOT_SYNC);
    if (m == 0x01) {
      delayNs<50>();
      trial--;
    }
  } while ((m == 0x01) && trial);            // wait for CPU --> 0

  IRQPin::high();                            // reset IRQ

  if (trial == 0) {
    // failed
    Serial1.printf("*E: schedSwitchcontext failed [%02X]\n", vContext);
    enableMMUInterrupt();                    // reenable MMU interrupts
    DebugPin::high();
    return false;
  }

  // now the context switch
  set6502State(sHALTED);                     // halt CPU

  setMMUContext(vContext);                   // switch context

  if (statusContext[vContext] && (!vForce)) {
    writeCmdSlot(CMD_SLOT_STAT, 0x01);       // context switch
  }
  else {
    writeCmdSlot(CMD_SLOT_STAT, 0x02);       // reset
    statusContext[vContext] = true;
  }

  writeCmdSlot(CMD_SLOT_SYNC, 0x01);         // ==> 1, ACK

  set6502State(sRUNNING);                    // continue CPU

  enableMMUInterrupt();

  return true;
}

/// <summary>
///
/// </summary>
void initScheduler() {

}
