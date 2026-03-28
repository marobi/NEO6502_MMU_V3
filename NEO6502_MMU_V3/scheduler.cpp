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
static void schedHelp(const char *str) {
  uint8_t regs[7];

  snoop_read6502Memory(0x0228, 7, regs);

  Serial1.printf("%sPC=%02X%02X SR=%02X A=%02X X=%02X Y=%02X SP=%02X\n", str, regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6]);
}

/// <summary>
/// 
/// </summary>
/// <param name="vContext"></param>
bool schedSwitchcontext(const uint8_t vContext, const bool vForce) {
  uint8_t trial;
  uint8_t buf[1];

  Serial1.printf("*I: SWITCH: CTX%1X -> CTX%1X %s\n", getMMUContext(),  vContext, (vForce) ? "RESET" : "RUN");

  DebugPin::low();

  buf[0] = 1;
  snoop_write6502Memory(0xD004, 1, buf);

  gen6502IRQ();                                // gen interrupt

  trial = 50;
  do {
    delayNs<125>();
    snoop_read6502Memory(0xD004, 1, buf);
    trial--;
  } while ((buf[0] == 1) && (trial > 0));      // wait for CPU --> 0

  if (trial == 0) {
    // failed
    Serial1.printf("*E: schedSwitchcontext failed [%02X]\n", vContext);
    return false;
  }
  
  DebugPin::high();

  // now the context switch
  set6502State(sHALTED);                       // halt CPU

  setMMUContext(vContext);                     // switch context

  DebugPin::low();

    if (statusContext[vContext] && (! vForce)) {
      buf[0] = 1;
      snoop_write6502Memory(0xD003, 1, buf);   // context switch
//      Serial1.printf("*I: schedSwitchcontext: SWITCH [%02X]\n", vContext);
    }
    else {
      buf[0] = 2;
      snoop_write6502Memory(0xD003, 1, buf);   // reset
      statusContext[vContext] = true;
//      Serial1.printf("*I: schedSwitchcontext: RESET [%02X]\n", vContext);
    }

    buf[0] = 1;
    snoop_write6502Memory(0xD004, 1, buf);     // ==> 1, ACK

    set6502State(sRUNNING);                    // continue CPU

  DebugPin::high();

  return true;
}

/// <summary>
///
/// </summary>
void initScheduler() {

}
