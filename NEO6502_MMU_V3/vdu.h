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

#define DEFAULT_COLOR   15      // WHITE
#define DEFAULT_BG_COLOR 4      // DARK BLUE
#define DEFAULT_MODE     0      // VDU display mode (0-7)

#define CURSOR_BLINK_INTERVAL_MS 600    // MS

// the 16 standard colors of the 256 in the palette
#define BLACK    0
#define MAROON   1
#define GREEN    2
#define OLIVE    3
#define NAVY     4
#define PURPLE   5
#define TEAL     6
#define SILVER   7
#define GRAY     8
#define RED      9
#define LIME    10
#define YELLOW  11
#define BLUE    12
#define FUCHSIA 13
#define AQUA    14
#define WHITE   15

////////////////////////////////////////////////////////////////////////
#define IDX_CURSOR       254   // palette index for cursor color
#define WIDTH            320   // TBD display.width()
#define HEIGHT           240   // TBD display.height()
#define FONT_CHAR_WIDTH  5  
#define FONT_CHAR_HEIGHT 7
#define FONT_CELL_WIDTH  (FONT_CHAR_WIDTH + 1)
#define FONT_CELL_HEIGHT (FONT_CHAR_HEIGHT + 1)
#define ROWS             (HEIGHT / FONT_CELL_HEIGHT)
#define COLS             (WIDTH / FONT_CELL_WIDTH)

/// <summary>
/// cursor styles
/// </summary>
typedef enum {
  cBLOCK = 0,
  cUNDERLINE
} cursor_shape_t;


/// <summary>
/// VDU characteristics
/// </summary>
typedef struct {
  boolean        visibleCursor;
  cursor_shape_t cursorShape;
  boolean        blinkCursor;
  boolean        textMode;
  boolean        geoAspect;
  boolean        autoScroll;
  boolean        textWrap;
  boolean        localEcho;
  boolean        ucaseOnly;
  boolean        crlf;
} vdu_mode_t;

extern const vdu_mode_t* vduMode;              // treat as RO

extern void setCursor(const boolean);
extern void resetDisplay(const uint8_t);

extern void taskVDU(void);

extern void setTColor(const uint8_t);
extern void setBGColor(const uint8_t);

extern void alterCursor(const cursor_shape_t);
extern void moveCursor(const uint16_t, const uint16_t);

extern void vduSetMode(const uint8_t);

extern void cmdClearDisplay();

extern void vduPutc(const uint8_t);
extern void vduPrintStr(const char*);
extern void vduPrintf(char const* fmt, ...);

extern uint8_t vduReadc(const uint16_t, const uint16_t);
extern void vduGetScreenline(const uint8_t, uint8_t*);
extern void vduGetCurrentScreenline(uint8_t* buffer);

extern void initVDU();
