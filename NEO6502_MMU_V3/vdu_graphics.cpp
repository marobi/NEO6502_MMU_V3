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

#include <Adafruit_dvhstx.h>
#include "config.h"
#include "vdu.h"
#include "vdu_graphics.h"

// our display
extern DVHSTX8 display;

typedef void (*cmdFunction)(void);

static vdu_registers_t  vduRegistersDefault;                   // default register array
static vdu_registers_t* vduRegisters = &vduRegistersDefault;   // ptr to registers array
static uint8_t          vduGColor    = DEFAULT_COLOR;          // graphics FG color
static uint8_t          vduDrawmode  = 0x00;                   // graphics draw mode, outline

// convenience
#define REG0    (vduRegisters->reg[0])      // X | X1
#define REG1    (vduRegisters->reg[1])      // Y | Y1
#define REG2    (vduRegisters->reg[2])      // W | R | X2
#define REG3    (vduRegisters->reg[3])      // H | Y2
#define REG4    (vduRegisters->reg[4])      // R | X3
#define REG5    (vduRegisters->reg[5])      // Y3

#define REG6    (vduRegisters->reg[6])      // M | I | V
#define REG7    (vduRegisters->reg[7])      // FG | BG | C
#define REG8    (vduRegisters->reg[8])      // I/O

/// <summary>
/// 
/// </summary>
void dumpVDURegisterSet() {
  for (uint8_t r = 0; r < NUM_VDU_REGISTERS; r++) {
    Serial1.printf("R%d = %04d\n", r, vduRegisters->reg[r]);
  }
}

/// <summary>
/// 
/// </summary>
/// <param name="vRegisters"></param>
void setVDURegisterSet(vdu_registers_t* vRegisters) {
  vduRegisters = vRegisters;
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint8_t getPixel() {
  return display.getPixel(REG0 % WIDTH, REG1 % HEIGHT);
}

/// <summary>
/// 
/// </summary>
static void drawPixel() {
  display.drawPixel(REG0 % WIDTH, REG1 % HEIGHT, vduGColor);
}

/// <summary>
/// 
/// </summary>
static void drawLine() {
  display.drawLine(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, vduGColor);
}

/// <summary>
/// 
/// </summary>
static void drawRect() {
  //    Serial1.printf("*D: RECT %04x %04x %04x %04x\n", vduRegisters->x, vduRegisters->y, vduRegisters->w, vduRegisters->h);
  switch (vduDrawmode) {
  case 0:
    display.drawRect(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, vduGColor);
    break;
  default:
    display.fillRect(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, vduGColor);
    break;
  }
}

/// <summary>
/// 
/// </summary>
static void drawRectR() {
  //    Serial1.printf("*D: RECT %04x %04x %04x %04x\n", vduRegisters->x, vduRegisters->y, vduRegisters->w, vduRegisters->h);
  switch (vduDrawmode) {
  case 0:
    display.drawRoundRect(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, REG4, vduGColor);
    break;
  default:
    display.fillRoundRect(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, REG4, vduGColor);
    break;
  }
}

/// <summary>
/// 
/// </summary>
static void drawCircle() {
  //    Serial1.printf("*D: CIRC %04x %04x %04x\n", vduRegisters->x, vduRegisters->y, vduRegisters->w);
  switch (vduDrawmode) {
  case 0:
    if (vduMode->geoAspect)
      display.drawEllipse(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, (REG3 * 4 / 3) % WIDTH, vduGColor);
    else
      display.drawCircle(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, vduGColor);
    break;
  default:
    if (vduMode->geoAspect)
      display.drawEllipse(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, (REG3 * 4 / 3) % WIDTH, vduGColor);
    else
      display.drawCircle(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, vduGColor);
    break;
  }
}

/// <summary>
/// 
/// </summary>
static void drawTriangle() {
  //    Serial1.printf("*D: TRI %04x %04x %04x %04x %04x %04x\n", vduRegisters->x, vduRegisters->y, vduRegisters->w, vduRegisters->h, vduRegisters->a, vduRegisters->b);
  switch (vduDrawmode) {
  case 0:
    display.drawTriangle(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, REG4 % WIDTH, REG5 % HEIGHT, vduGColor);
    break;
  default:
    display.fillTriangle(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, REG4 % WIDTH, REG5 % HEIGHT, vduGColor);
    break;
  }
}

/// <summary>
/// 
/// </summary>
static void cmdDMode() {
  vduDrawmode = vduRegisters->block[P6L];
}

/// <summary>
/// 
/// </summary>
static void cmdCLS() {
  cmdClearDisplay();
}

/// <summary>
/// 
/// </summary>
static void cmdGColor() {
  vduGColor = vduRegisters->block[P7L];
}

/// <summary>
/// 
/// </summary>
static void cmdVDUMode() {
  vduSetMode(vduRegisters->block[P6L]);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary>
/// 
/// </summary>
static void cmdVoid() {
  Serial1.printf("*E: VDU illegal command\n");
}

/// <summary>
/// 
/// </summary>
static void cmdTBD() {
  Serial1.printf("*E: VDU TBD command\n");
}

/// <summary>
/// 
/// </summary>
/// <param name="val"></param>
void vduSetReg(const uint8_t vRegister, const uint16_t vVal) {
  Serial1.printf("*D: vduSetReg %02d = %4d\n", vRegister, vVal);

  if (vRegister < NUM_VDU_REGISTERS)
    vduRegisters->reg[vRegister] = vVal;
  else
    Serial1.printf("*E: vduSetReg invalid reg [%02d] = [%4dx]\n", vRegister, vVal);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary>
/// table with avaialble vdu command functions
/// </summary>
static cmdFunction cmdCommands[MAX_VDU_COMMANDS] = {
  cmdVoid,      // 0
  cmdCLS,       // 1
  cmdTBD,       // 2
  drawPixel,    // 3
  drawLine,     // 4
  drawRect,     // 5
  drawCircle,   // 6
  drawTriangle, // 7
  drawRectR,    // 8
  cmdDMode,     // 9
  cmdGColor,    // B
  cmdTBD,       // D
  cmdTBD,       // C
  cmdTBD,       // D
  cmdVoid,      // E
  cmdVoid,      // F
  cmdTBD,       // 10
  cmdTBD,       // 11
  cmdVoid, // 12
  cmdVoid, // 13
  cmdVoid, // 14
  cmdVoid, // 15
  cmdVoid, // 16
  cmdVoid, // 17
  cmdVoid, // 18
  cmdVoid, // 19
  cmdVoid, // 1A
  cmdVoid, // 1B
  cmdVoid, // 1C
  cmdVoid, // 1D
  cmdVoid, // 1E
  cmdVDUMode,  // 1F
};

/// <summary>
/// exec a command on the display
/// </summary>
/// <param name="vCmd"></param>
void vduSetCmd(const uint8_t vCmd) {
  Serial1.printf("*D: vduSetCmd %02d\n", vCmd);
  if (vCmd == CMD_SANE) {
    resetDisplay(0);
  }
  else {
    cmdFunction cmd = cmdCommands[vCmd % MAX_VDU_COMMANDS];
    cmd(); // lets do it
  }
}

/// <summary>
/// convenience function for test-purposes
/// perform a cmd with a set of registers
/// </summary>
/// <param name="vCmd"></param>
/// <param name="nRegs"></param>
/// <param name=""></param>
void vduSetCmdx(const uint8_t vCmd, const uint8_t nRegs, ...)
{
  va_list ap;
  va_start(ap, nRegs);

  for (uint8_t r = 0; r < (nRegs % NUM_VDU_REGISTERS); r++) {
    uint16_t v = (uint16_t)va_arg(ap, int); // promoted
    vduSetReg(r, v);
  }

  va_end(ap);

  vduSetCmd(vCmd);
}
