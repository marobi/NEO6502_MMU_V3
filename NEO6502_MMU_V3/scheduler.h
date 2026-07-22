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

/// <summary>
/// irq_source_t defines the possible sources of interrupts that can be generated for the 6502 CPU.
/// </summary>
enum irq_source_t {
  RP_SRC_NONE = 0,
  RP_SRC_TIMER,
  RP_SRC_MONITOR,
  RP_SRC_FS_DONE,
  RP_SRC_CONSOLE_BREAK
};

/// <summary>
/// irq_state_t defines the possible states of an interrupt request for the 6502 CPU, indicating whether an interrupt is pending or not.
/// </summary>
enum irq_state_t {
  RP_IRQ_NONE = 0,
  RP_IRQ_PENDING
};

extern bool gInMonitor;

bool genIRQ6502(irq_source_t vSrc);

/// <summary>
/// Queues one filesystem-completion interrupt for delivery to the 6502.
/// </summary>
void requestFSCompletionIRQ();

/// <summary>
/// Queues one console-break interrupt for delivery to the 6502.
/// </summary>
void requestConsoleBreakIRQ();

void stopIRQTimer();

void startIRQTimer(const unsigned long vPeriod);

void taskIRQTimer();

void taskScheduler();

void initScheduler();
