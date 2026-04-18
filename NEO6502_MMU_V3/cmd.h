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
//#define CMD_SLOT_OUTCHAR 0        // write to 6502      (obsolete)
//#define CMD_SLOT_INCHAR  1        // read from 6502     (obsolete)
#define CMD_SLOT_CMD     2        // cmd from 6502
#define CMD_SLOT_PARAM   3        // param from 6502

/// <summary>
/// 
/// </summary>
enum eCMD6502 {
  CMD6502_NONE = 0,
  CMD6502_ACK_IRQ,
  CMD6502_CONTEXT_SWITCH
};

void    initCmdInterface();

bool    getCommand6502(eCMD6502 &vCmd, uint8_t &vParam);

void    ackCommand6502();
