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
  RP_SRC_MONITOR
};

extern bool gInMonitor;

bool genIRQ6502(irq_source_t vSrc);

void stopIRQTimer();

void startIRQTimer(const uint16_t vPeriod);

void taskIRQTimer();

void taskScheduler();

void initScheduler();
