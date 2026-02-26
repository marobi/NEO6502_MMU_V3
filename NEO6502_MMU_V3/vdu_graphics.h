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

#define MAX_VDU_COMMANDS   32 
#define NUM_VDU_REGISTERS   9

// VDU commands
#define CMD_NONE    0x00
#define CMD_CLS     0x01    // CLS
#define CMD_MOVE    0x02    // MOVE
#define CMD_PIXEL   0x03    // PIXEL
#define CMD_LINE    0x04    // LINE
#define CMD_RECT    0x05    // RECT
#define CMD_CIRC    0x06    // CIRCLE
#define CMD_TRI     0x07    // TRIANGLE
#define CMD_RECTR   0x08    // ROUNDED RECT

#define CMD_MODE    0x09    // DRAW MODE
#define CMD_GCOLOR  0x0A    // GRAPHICS COLOR
#define CMD_TCOLOR  0x0B    // TEXT COLOR
#define CMD_BGCOLOR 0x0C    // BACKGROUND COLOR shared by text and graphics
#define CMD_PAL     0x0D    // PALETTE

#define CMD_VDU     0x1F    // VDU MODE

//#define CMD_SPRITE 0x10   // SPRITE 1cpp
//#define CMD_SDRAW  0x12   // Sprite DRAW
//#define CMD_SMOVE  0x14   // Srpite MOVE
//#define CMD_COLL   0x15   // Sprite COLLISION

//#define CMD_TILE   0x16   // TILE

#define CMD_SANE   0xFF     // SANE

//#define VDU_VER    0x1E     // VDU version
//#define VDU_STAT   0x1F     // Status register

#define R0       0
#define R1       1
#define R2       2
#define R3       3
#define R4       4
#define R5       5
#define R6       6
#define R7       7
#define R8       8

#define P0L      0
#define P0H      1
#define P1L      2
#define P1H      3
#define P2L      4
#define P2H      5
#define P3L      6
#define P3H      7
#define P4L      8
#define P4H      9
#define P5L      10
#define P5H      11
#define P6L      12
#define P6H      13
#define P7L      14
#define P7H      15
#define P8L      16
#define P8H      17

/////////////////////////////////////////////////////////////////////////////////////////////
/// <summary>
/// 
/// </summary>
typedef union {
  uint16_t reg[NUM_VDU_REGISTERS];        // × 16-bit
  uint8_t  block[NUM_VDU_REGISTERS * 2];  // same memory as bytes
} vdu_registers_t;

extern void dumpVDURegisterSet();

extern void setVDURegisterSet(vdu_registers_t*);

extern void vduSetReg(const uint8_t, const uint16_t);
extern void vduSetCmd(const uint8_t);

extern void vduSetCmdx(const uint8_t, const uint8_t, ...);
