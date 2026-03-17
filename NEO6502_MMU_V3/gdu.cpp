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
#include "gdu.h"

// our display
extern DVHSTX8 display;

typedef void (*cmdFunction)(void);

static gdu_registers_t  gduRegistersDefault;                   // default register array
static gdu_registers_t* gduRegisters = &gduRegistersDefault;   // ptr to registers array
static uint8_t          gduGColor    = DEFAULT_COLOR;          // graphics FG color
static uint8_t          gduDrawmode  = 0x00;                   // graphics draw mode, outline

// convenience
#define REG0    (gduRegisters->reg[0])      // X | X1
#define REG1    (gduRegisters->reg[1])      // Y | Y1
#define REG2    (gduRegisters->reg[2])      // W | R | X2
#define REG3    (gduRegisters->reg[3])      // H | Y2
#define REG4    (gduRegisters->reg[4])      // R | X3
#define REG5    (gduRegisters->reg[5])      // Y3

#define REG6    (gduRegisters->reg[6])      // M | I | V
#define REG7    (gduRegisters->reg[7])      // FG | BG | C
#define REG8    (gduRegisters->reg[8])      // I/O

/// <summary>
/// 
/// </summary>
void dumpGDURegisterSet() {
  for (uint8_t r = 0; r < NUM_GDU_REGISTERS; r++) {
    Serial1.printf("R%d = %04d\n", r, gduRegisters->reg[r]);
  }
}

/// <summary>
/// 
/// </summary>
/// <param name="vRegisters"></param>
void setGDURegisterSet(gdu_registers_t* vRegisters) {
  gduRegisters = vRegisters;
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
  display.drawPixel(REG0 % WIDTH, REG1 % HEIGHT, gduGColor);
}

/// <summary>
/// 
/// </summary>
static void drawLine() {
  display.drawLine(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, gduGColor);
}

/// <summary>
/// 
/// </summary>
static void drawRect() {
  //    Serial1.printf("*D: RECT %04x %04x %04x %04x\n", gduRegisters->x, gduRegisters->y, gduRegisters->w, gduRegisters->h);
  switch (gduDrawmode) {
  case 0:
    display.drawRect(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, gduGColor);
    break;
  default:
    display.fillRect(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, gduGColor);
    break;
  }
}

/// <summary>
/// 
/// </summary>
static void drawRectR() {
  //    Serial1.printf("*D: RECT %04x %04x %04x %04x\n", gduRegisters->x, gduRegisters->y, gduRegisters->w, gduRegisters->h);
  switch (gduDrawmode) {
  case 0:
    display.drawRoundRect(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, REG4, gduGColor);
    break;
  default:
    display.fillRoundRect(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, REG4, gduGColor);
    break;
  }
}

/// <summary>
/// 
/// </summary>
static void drawCircle() {
  //    Serial1.printf("*D: CIRC %04x %04x %04x\n", gduRegisters->x, gduRegisters->y, gduRegisters->w);
  switch (gduDrawmode) {
  case 0:
    if (vduMode->geoAspect)
      display.drawEllipse(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, (REG3 * 4 / 3) % WIDTH, gduGColor);
    else
      display.drawCircle(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, gduGColor);
    break;
  default:
    if (vduMode->geoAspect)
      display.drawEllipse(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, (REG3 * 4 / 3) % WIDTH, gduGColor);
    else
      display.drawCircle(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, gduGColor);
    break;
  }
}

/// <summary>
/// 
/// </summary>
static void drawTriangle() {
  //    Serial1.printf("*D: TRI %04x %04x %04x %04x %04x %04x\n", gduRegisters->x, gduRegisters->y, gduRegisters->w, gduRegisters->h, gduRegisters->a, gduRegisters->b);
  switch (gduDrawmode) {
  case 0:
    display.drawTriangle(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, REG4 % WIDTH, REG5 % HEIGHT, gduGColor);
    break;
  default:
    display.fillTriangle(REG0 % WIDTH, REG1 % HEIGHT, REG2 % WIDTH, REG3 % HEIGHT, REG4 % WIDTH, REG5 % HEIGHT, gduGColor);
    break;
  }
}

/// <summary>
/// 
/// </summary>
static void cmdDMode() {
  gduDrawmode = gduRegisters->block[P6L];
}

/// <summary>
/// 
/// </summary>
static void cmdCLS() {
  cmdClearScreen();
}

/// <summary>
/// 
/// </summary>
static void cmdGColor() {
  gduGColor = gduRegisters->block[P7L];
}

/// <summary>
/// 
/// </summary>
static void cmdGDUMode() {
  vduSetMode(gduRegisters->block[P6L]);
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
void gduSetReg(const uint8_t vRegister, const uint16_t vVal) {
  Serial1.printf("*D: vduSetReg %02d = %4d\n", vRegister, vVal);

  if (vRegister < NUM_GDU_REGISTERS)
    gduRegisters->reg[vRegister] = vVal;
  else
    Serial1.printf("*E: vduSetReg invalid reg [%02d] = [%4dx]\n", vRegister, vVal);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary>
/// table with avaialble vdu command functions
/// </summary>
static cmdFunction cmdCommands[MAX_GDU_COMMANDS] = {
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
  cmdGDUMode,  // 1F
};

/// <summary>
/// exec a command on the display
/// </summary>
/// <param name="vCmd"></param>
void gduSetCmd(const uint8_t vCmd) {
  Serial1.printf("*D: gduSetCmd %02d\n", vCmd);
  if (vCmd == CMD_SANE) {
    resetDisplay(0);
  }
  else {
    cmdFunction cmd = cmdCommands[vCmd % MAX_GDU_COMMANDS];
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
void gduSetCmdx(const uint8_t vCmd, const uint8_t nRegs, ...)
{
  va_list ap;
  va_start(ap, nRegs);

  for (uint8_t r = 0; r < (nRegs % NUM_GDU_REGISTERS); r++) {
    uint16_t v = (uint16_t)va_arg(ap, int); // promoted
    gduSetReg(r, v);
  }

  va_end(ap);

  gduSetCmd(vCmd);
}
