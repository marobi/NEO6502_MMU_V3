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
#include "vdu_interface.h"
#include "vdu_graphics.h"

/// <summary>
/// 
/// </summary>
typedef enum {
  cGET = 0,
  cPUT,
  cNOC               // NO Command
} vdu_cmd_type_t;

/// <summary>
/// 
/// </summary>
typedef struct {
  vdu_cmd_type_t cmd_type;
  uint8_t register_offset;
  uint8_t num_bytes;
} vdu_cmd_exec_type_t;

/// <summary>
/// param table to process vdu commands
/// </summary>
static vdu_cmd_exec_type_t vduCmds[MAX_VDU_COMMANDS] = {
  { cNOC, 0,  0},            // 0 CMD_VOID
  { cGET, 0,  0},            // 1 CMD_CLS
  { cGET, 0,  4},            // 2 CMD_MOVE
  { cGET, 0,  8},            // 3 CMD_PIXEL
  { cGET, 0,  8},            // 4 CMD_LINE
  { cGET, 0,  8},            // 5 CMD_RECT
  { cGET, 0,  6},            // 6 CMD_CIRC
  { cGET, 0, 12},            // 7 CMD_TRI
  { cGET, 0, 10},            // 8 CMD_RECTR
  { cGET, 12, 1},            // 9 CMD_MODE
  { cGET, 14, 1},            // A CMD_GCOLOR
  { cGET, 14, 1},            // B CMD_TCOLOR
  { cGET, 14, 1},            // C CMD_BGCOLOR
  { cGET, 12, 4},            // D CMD_PAL
  { cNOC, 0,  0},            // E CMD_VOID
  { cNOC, 0,  0},            // F CMD_VOID
  { cNOC, 0,  0},            // 10 CMD_VOID
  { cNOC, 0,  0},            // 11 CMD_VOID
  { cNOC, 0,  0},            // 12 CMD_VOID
  { cNOC, 0,  0},            // 13 CMD_VOID
  { cNOC, 0,  0},            // 14 CMD_VOID
  { cNOC, 0,  0},            // 15 CMD_VOID
  { cNOC, 0,  0},            // 16 CMD_VOID
  { cNOC, 0,  0},            // 17 CMD_VOID
  { cNOC, 0,  0},            // 18 CMD_VOID
  { cNOC, 0,  0},            // 19 CMD_VOID
  { cNOC, 0,  0},            // 1A CMD_VOID
  { cNOC, 0,  0},            // 1B CMD_VOID
  { cNOC, 0,  0},            // 1C CMD_VOID
  { cNOC, 0,  0},            // 1D CMD_VOID
  { cNOC, 0,  0},            // 1E CMD_VOID
  { cGET, 12, 1},            // 1F CMD_VDU
};

static vdu_registers_t vduRegisters;

/// <summary>
/// dummy
/// </summary>
/// <param name="vBuffer"></param>
/// <param name="vAddress"></param>
/// <param name="vLen"></param>
void readCpuMemory(const uint8_t vBuffer[], const uint16_t vAddress, const uint8_t vLen) {

}

/// <summary>
/// dummy
/// </summary>
/// <param name="vAddress"></param>
/// <param name="vData"></param>
void writeCpuMemory(const uint16_t vAddress, const uint8_t vData) {

}

/// <summary>
/// 
/// </summary>
void initVDUInterface() {
  setVDURegisterSet(&vduRegisters);
}

/// <summary>
/// 
/// </summary>
/// <param name="vOffset"></param>
/// <param name="vLen"></param>
void loadVDURegisters(const uint8_t vOffset, const uint8_t vLen) {
  readCpuMemory(&vduRegisters.block[vOffset], VDU_REGISTER_BASE + vOffset, vLen);
}

/// <summary>
/// 
/// </summary>
void execVDUCommand() {
  uint8_t lCmd, lOffset, lLen = 0;

  readCpuMemory(&lCmd, VDU_CMD, 1);      // get vdu command
  Serial1.printf("*D: execVDUCommand: cmd = [%02x]", lCmd);

  switch (vduCmds[lCmd].cmd_type) {
  case cGET:
    lOffset = vduCmds[lCmd].register_offset;
    lLen    = vduCmds[lCmd].num_bytes;
    loadVDURegisters(lOffset, lLen);

    writeCpuMemory(VDU_CMD, CMD_NONE);
    
    vduSetCmd(lCmd);
    
    break;

  case cPUT:
    Serial1.printf("*E: execVDUCommand: TBD [%02x]", lCmd);
      break;
  
  case cNOC:
  default:
    Serial1.printf("*E: execVDUCommand: invalid cmd [%02x]\n", lCmd);
    break;
  }
}
