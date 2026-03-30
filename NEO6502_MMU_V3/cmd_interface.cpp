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
#include <Arduino.h>
#include "config.h"
#include "cmd_interface.h"
#include "cmd.h"
#include "gdu.h"

/// <summary>
/// 
/// </summary>
typedef enum {
  cNOC = 0,               // NO Command
  cGET,
  cPUT
} t_cmd_type;

/// <summary>
/// 
/// </summary>
typedef struct {
  uint8_t groupid;
  uint8_t cmdid;
  t_cmd_type cmd_type;
  uint8_t offset_reg;
  uint8_t num_regs;
  bool (*callback)();
} t_cmd_exec_type;

bool cbDummy() {

}

/// <summary>
/// param table to process vdu commands
/// </summary>
static t_cmd_exec_type InterfaceCmds[] = {
  { CI_GROUP_GDU, 0x01, cGET, 0,   0,  cbDummy },            // 1 CMD_CLS
  { CI_GROUP_GDU, 0x02, cGET, 0,   4,  cbDummy },            // 2 CMD_MOVE
  { CI_GROUP_GDU, 0x03, cGET, 0,   8,  cbDummy },            // 3 CMD_PIXEL
  { CI_GROUP_GDU, 0x04, cGET, 0,   8,  cbDummy },            // 4 CMD_LINE
  { CI_GROUP_GDU, 0x05, cGET, 0,   8,  cbDummy },            // 5 CMD_RECT
  { CI_GROUP_GDU, 0x06, cGET, 0,   6,  cbDummy },            // 6 CMD_CIRC
  { CI_GROUP_GDU, 0x07, cGET, 0,  12,  cbDummy },            // 7 CMD_TRI
  { CI_GROUP_GDU, 0x08, cGET, 0,  10,  cbDummy },            // 8 CMD_RECTR
  { CI_GROUP_GDU, 0x09, cGET, 12,  1,  cbDummy },            // 9 CMD_MODE
  { CI_GROUP_GDU, 0x0A, cGET, 14,  1,  cbDummy },            // A CMD_GCOLOR
  { CI_GROUP_GDU, 0x0B, cGET, 14,  1,  cbDummy },            // B CMD_TCOLOR
  { CI_GROUP_GDU, 0x0C, cGET, 14,  1,  cbDummy },            // C CMD_BGCOLOR
  { CI_GROUP_GDU, 0x0D, cGET, 12,  4,  cbDummy },            // D CMD_PAL

  { CI_GROUP_GDU, 0x12, cGET, 12,  1,  cbDummy },            // 1F CMD_VDU

  { CI_GROUP_UNK, 0x00, cNOC, 0,   0,  cbDummy }             // last entry
};

static uint8_t ciRegisters[MAX_REGISTERS];

/// <summary>
/// 
/// </summary>
/// <param name="vOffset"></param>
/// <param name="vLen"></param>
void loadCIRegisters(const uint8_t vOffset, const uint8_t vLen) {
  snoop_read6502Memory(CPU_REGISTER_BASE + vOffset, vLen, ciRegisters);
}

/// <summary>
/// 
/// </summary>
void execCIcommand() {
  uint8_t lCmd = 0;
  uint8_t lOffset = 0;
  uint8_t lLen = 0;

  readCpuMemory(&lCmd, GDU_CMD, 1);      // get vdu command
  Serial1.printf("*D: execGDUCommand: cmd = [%02x]", lCmd);

  switch (gduCmds[lCmd].cmd_type) {
  case cGET:
    lOffset = gduCmds[lCmd].register_offset;
    lLen    = gduCmds[lCmd].num_bytes;
    loadCIRegisters(lOffset, lLen);

    writeCpuMemory(GDU_CMD, CMD_NONE);
    
    gduSetCmd(lCmd);
    
    break;

  case cPUT:
    Serial1.printf("*E: execGDUCommand: TBD [%02x]", lCmd);
      break;
  
  case cNOC:
  default:
    Serial1.printf("*E: execGDUCommand: invalid cmd [%02x]\n", lCmd);
    break;
  }
}

/// <summary>
/// 
/// </summary>
void initCPInterface() {

}
