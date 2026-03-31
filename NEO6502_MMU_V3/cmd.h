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

#define CMD_SLOT_BASE    0xD000   // sync with memory.ini
#define CMD_SLOT_OUTCHAR 0        // write to 6502
#define CMD_SLOT_INCHAR  1        // read from 6502
#define CMD_SLOT_CMD     2        // cmd from 6502
#define CMD_SLOT_STAT    3        // status to 6502
#define CMD_SLOT_SYNC    4        // sync with 6502 (context switching)

void initCmdInterface();

uint8_t readCmdSlot(const uint8_t vSlot);

void writeCmdSlot(const uint8_t vSlot, uint8_t vData);

uint8_t inChar6502();

bool outChar6502(const uint8_t vChar);

bool outCharAvailable6502();

void outCharBlocking6502(const uint8_t vChar);

uint8_t getCommand6502();

void ackCommand6502();
