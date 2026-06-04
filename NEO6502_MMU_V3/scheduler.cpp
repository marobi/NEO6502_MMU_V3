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
#include "mailbox.h"

bool gInMonitor = false;

/// <summary>
/// 
/// </summary>
/// <param name="vContext"></param>
void schedSwitchcontext(const uint8_t vContext) {
  //  uint8_t c = getMMUContext();

  disableMMUInterrupt();

  sysstate_t s = get6502State();

  // now the context switch
  set6502State(sHALTED);        // halt CPU

  setMMUContext(vContext);      // switch context

  set6502State(s);              // continue CPU

  enableMMUInterrupt();

  //  Serial1.printf("*I: CTX%1X -> CTX%1X\n", c, vContext);
}

static bool gIntrPending = false;

/// <summary>
/// 
/// </summary>
/// <returns></returns>
bool genIRQ6502(irq_source_t vSrc) {
  if (gIntrPending) {
    Serial1.println("*E: genIRQ6502: pending IRQ not ACKed");
    return false;
  }

  if (snoop_read6502MemoryLoc(RP_IRQ_STATE) == RP_IRQ_NONE) {     // check if no pending IRQ
    snoop_write6502MemoryLoc(RP_IRQ_SOURCE, (uint8_t)vSrc);
    snoop_write6502MemoryLoc(RP_IRQ_STATE, RP_IRQ_PENDING);
    IRQPin::low();
    DebugPin::low();
    gIntrPending = true;
    return true;
  }
  else {
    return false;
  }
}

static bool irqTimerEnabled = false;
static unsigned long irqTimerInterval;
static unsigned long irqLastTS;

/// <summary>
/// 
/// </summary>
void stopIRQTimer() {
  irqTimerEnabled = false;
  IRQPin::high();
  DebugPin::high();
  snoop_write6502MemoryLoc(RP_IRQ_SOURCE, RP_SRC_NONE);
  snoop_write6502MemoryLoc(RP_IRQ_STATE, RP_IRQ_NONE);
  gIntrPending = false;

  Serial1.println("*D: stopIRQTimer");
}

/// <summary>
/// 
/// </summary>
/// <param name="vPeriod"></param>
void startIRQTimer(const uint16_t vPeriod) {
  if (vPeriod < 65535) {
    irqTimerInterval = vPeriod;
    irqTimerEnabled = true;
    irqLastTS = millis();
    snoop_write6502MemoryLoc(RP_IRQ_SOURCE, RP_SRC_NONE);
    snoop_write6502MemoryLoc(RP_IRQ_STATE, RP_IRQ_NONE);
    IRQPin::high();
    DebugPin::high();

    Serial1.printf("*D: startIRQTimer: %d ms\n", vPeriod);
  }
  else
    stopIRQTimer();
}

static uint8_t taskTimerCounter = 0;

/// <summary>
/// 
/// </summary>
void taskIRQTimer() {
  // check if IRQ is pending
  if (gIntrPending) {
    // check if source is ACKed by 6502
    if (snoop_read6502MemoryLoc(RP_IRQ_SOURCE) == RP_SRC_NONE) {
      // ACKed, clear pending state
      gIntrPending = false;
      IRQPin::high();
      DebugPin::high();
      snoop_write6502MemoryLoc(RP_IRQ_STATE, RP_IRQ_NONE);
    }

    return;
  }

  if (irqTimerEnabled) {
    if (millis() >= (irqLastTS + irqTimerInterval)) {
      // gen IRQ
      if (! genIRQ6502(RP_SRC_TIMER))
        taskTimerCounter++;
      else
        taskTimerCounter = 0;

      irqLastTS = millis();
    }

    if (taskTimerCounter > 3)
      Serial1.println("*E: taskIRQTimer: IRQ gen failed");
  }
}

/// <summary>
/// 
/// </summary>
void taskScheduler() {
  uint8_t lCmd;
  uint8_t lParam;

  if (!getCommand6502(lCmd, lParam))
    return;

  switch (lCmd) {
  case CMD6502_NONE:
    break;

  case CMD6502_CONTEXT_SWITCH:
    schedSwitchcontext(lParam);

    ackCommand6502();
    break;

  default:
    break;
  }
}

/// <summary>
///
/// </summary>
void initScheduler() {
  snoop_write6502MemoryLoc(RP_IRQ_SOURCE, RP_SRC_NONE);
  snoop_write6502MemoryLoc(RP_IRQ_STATE, RP_IRQ_NONE);
}
