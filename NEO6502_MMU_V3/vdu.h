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
//#define DEFAULT_COLOR     0
//#define DEFAULT_BG_COLOR  250
#define DEFAULT_MODE      0      // VDU display mode (0-9)
#define NUMBER_OF_MODES  10      // number of VDU modes

#define CURSOR_BLINK_INTERVAL_MS 400    // MS
#define SCROLL_INTERVAL_MS       8

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
#define IDX_CURSOR_FG    253   // palette index for cursor FG color
#define IDX_CURSOR_BG    254   // palette index for cursor BG color

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
  boolean        smoothScroll;
  boolean        textWrap;
  boolean        localEcho;
  boolean        ucaseOnly;
  boolean        crlf;
  boolean        screenMode;
} vdu_mode_t;

extern const vdu_mode_t* vduMode;              // treat as RO

void setCursor(const boolean);
void resetDisplay(const uint8_t);

void taskVDU(void);

void setTColor(const uint8_t);
void setBGColor(const uint8_t);

void alterCursor(const cursor_shape_t);
void moveCursor(const uint16_t, const uint16_t);

void vduSaveCursor();
void vduRestoreCursor();

bool getAsScreenMode();
void setAsScreenMode(const bool);

void vduSetMode(const uint8_t);

void cmdClearScreen();

void vduPutc(const uint8_t);
void vduPrintStr(const char*);
uint16_t vduPrintBuf(const uint8_t*, const uint16_t);
void vduPrintf(char const* fmt, ...);

uint8_t vduReadc(const uint16_t, const uint16_t);
void vduGetScreenline(const uint8_t, uint8_t*);
void vduGetCurrentScreenline(uint8_t* buffer);

/// <summary>
/// Enable or disable the RP-side VDU mouse overlay. This is VDU-local only.
/// </summary>
/// <param name="enabled">true to show the overlay, false to hide it.</param>
void vduMouseEnable(bool enabled);

/// <summary>
/// Update the RP-side VDU mouse overlay from a relative USB HID mouse report.
/// This does not report mouse state to the 6502. In text mode, a left-button
/// press edge moves the existing VDU cursor to the mouse cell.
/// </summary>
/// <param name="dx">relative X movement from the HID report.</param>
/// <param name="dy">relative Y movement from the HID report.</param>
/// <param name="buttons">current HID button bitmask.</param>
void vduMouseUpdate(int8_t dx, int8_t dy, uint8_t buttons);

/// <summary>
/// Return whether the RP-side VDU mouse overlay is enabled.
/// </summary>
/// <returns>true when enabled.</returns>
bool vduMouseIsEnabled();

void initVDU();
