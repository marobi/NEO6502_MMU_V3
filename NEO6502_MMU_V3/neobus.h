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

#include "pins.h"

void setupNEOBus();

uint16_t readCPUBusAddress();
uint16_t readCPUAddress();

void writeCPUAddressH(const uint8_t);

uint8_t read6502Data();

uint8_t read6502Memory(const uint16_t);

void write6502Memory(const uint16_t, const uint8_t);

uint8_t snoop_read6502MemoryLoc(const uint16_t vAddress);

void snoop_read6502Memory(const uint16_t, const uint32_t, uint8_t*);

void snoop_write6502MemoryLoc(const uint16_t vAddress, uint8_t vData);

void snoop_write6502Memory(const uint16_t, uint32_t, const uint8_t*);

#if 1
void testBUS();
#endif
