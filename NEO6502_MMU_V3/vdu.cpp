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
#include "palette.h"
#include "vdu.h"

// NEO6502_MMU settings: resolution 320x240 by 256 colors, single buffer
DVHSTXPinout pinConfig = { 14, 18, 16, 12 };
DVHSTX8 display(pinConfig, DVHSTX_RESOLUTION_320x240, false);

// visibleCursor cursorStyle blinkCursor textMode geoAspect autoScroll textWrap localEcho ucaseOnly  CRLF
static const vdu_mode_t vduModes[8] = {
  {true,         cUNDERLINE, true,       true,    false,    true,      false,   false,    false,     true  }, // Mode 0 text mode + underline cursor
  {true,         cBLOCK    , true,       true,    false,    true,      false,   false,    false,     true  }, // Mode 1 text model + block cursor
  {false,        cUNDERLINE, false,      true,    false,    true,      false,   false,    false,     true  }, // Mode 2 text mode no cursor
  {false,        cUNDERLINE, false,      true,    false,    false,     false,   false,    false,     true  }, // Mode 3 text mode no cursor no scroll
  {false,        cUNDERLINE, false,      false,   false,    false,     false,   false,    false,     true  }, // Mode 4 text graphics mode no cursor no scroll
  {true,         cUNDERLINE, true,       true,    true,     true,      false,   false,    false,     true  }, // Mode 5 = mode 0 + geo aspect correction
  {false,        cUNDERLINE, false,      false,   true,     false,     false,   false,    false,     true  }, // Mode 6 = mode 4 + geo aspect correction
  {false,        cUNDERLINE, false,      true,    true,     false,     false,   false,    false,     true  }, // Mode 7 = mode 3 + geo aspect correction
};

const vdu_mode_t*  vduMode = &vduModes[DEFAULT_MODE];

/// <summary>
/// cursor definition
/// </summary>
typedef struct {
  uint8_t col;
  uint8_t row;
  bool    visible;
  bool    blink_on;
} cursor_def_t;

// our text cursor
static cursor_def_t gCursor = {
    .col = 0,
    .row = 0,
    .visible = true,
    .blink_on = false,
};

// our screen buffer
static uint8_t        textBuffer[ROWS][COLS];

static uint8_t        currentColor   = DEFAULT_COLOR;
static uint8_t        currentBGColor = DEFAULT_BG_COLOR;

/// <summary>
/// helper routine to convert RGB to BRG
/// </summary>
/// <param name="rgb"></param>
/// <returns></returns>
inline __attribute__((always_inline))
uint32_t rgb_to_brg(uint32_t rgb) {
  uint32_t r = (rgb >> 16) & 0xFF;
  uint32_t g = (rgb >> 8) & 0xFF;
  uint32_t b = rgb & 0xFF;

  return (b << 16) | (r << 8) | g;
}

/// <summary>
/// helper to init text buffer
/// </summary>
/// <param name=""></param>
inline __attribute__((always_inline))
void initTextbuffer(void)
{
  for (uint8_t r = 0; r < ROWS; r++) {
    for (uint8_t c = 0; c < COLS; c++) {
      textBuffer[r][c] = ' ';
    }
  }
}

/// <summary>
/// set current text color (by pallette index)
/// </summary>
/// <param name="vColor"></param>
void setBGColor(const uint8_t vColor) {
  currentBGColor = vColor;
  display.setTextColor(currentColor, vColor);
}

/// <summary>
/// set current text color (by pallette index)
/// </summary>
/// <param name="vColor"></param>
void setTColor(const uint8_t vColor) {
  currentColor = vColor;
  display.setTextColor(vColor);
}


/// <summary>
/// show cursor 
/// </summary>
inline __attribute__((always_inline))
void showCursor() {
  if (!gCursor.visible) {
    uint8_t c = textBuffer[gCursor.row][gCursor.col];

    uint16_t x = gCursor.col * FONT_CELL_WIDTH;
    uint16_t y = gCursor.row * FONT_CELL_HEIGHT;

    switch (vduMode->cursorStyle) {
    case cBLOCK:
      display.fillRect(
        x,
        y,
        FONT_CELL_WIDTH,
        FONT_CELL_HEIGHT,
        IDX_CURSOR
      );

      if (c != ' ') {
        display.setTextColor(IDX_CURSOR, currentBGColor);
        display.setCursor(x, y);
        display.write(c);
      }
      break;

    case cUNDERLINE:
      display.fillRect(
        x,
        y + FONT_CELL_HEIGHT - 2,
        FONT_CELL_WIDTH,
        2,
        IDX_CURSOR
      );     
      break;
    }

    gCursor.visible = true;
  }
}

/// <summary>
/// hide cursor
/// </summary>
inline __attribute__((always_inline))
void hideCursor() {
  // remove optional cursor
  if (gCursor.visible) {
    uint16_t x = gCursor.col * FONT_CELL_WIDTH;
    uint16_t y = gCursor.row * FONT_CELL_HEIGHT;
    uint8_t c = textBuffer[gCursor.row][gCursor.col];

    // restore full cell background
    display.fillRect(
      x, y,
      FONT_CELL_WIDTH, FONT_CELL_HEIGHT,
      currentBGColor
    );

    // redraw glyph if needed
    if (c != ' ') {
      display.setTextColor(currentColor, currentBGColor);
      display.setCursor(x, y);
      display.write(c);
    }

    gCursor.visible = false;
  }
}

/// <summary>
/// control show/hide cursor
/// </summary>
/// <param name="vVisible"></param>
inline __attribute__((always_inline))
void setCursor(const boolean vShow) {
  if ((vduMode->textMode) && (vduMode->visibleCursor)) {
    if (vShow)
      showCursor();
    else
      hideCursor();
  }
}

/// <summary>
/// move cursor
/// </summary>
/// <param name="x"></param>
/// <param name="y"></param>
void moveCursor(const uint16_t x, const uint16_t y) {
  if (vduMode->textMode) {
    setCursor(false);

    gCursor.col = (x < COLS) ? x : COLS - 1;
    gCursor.row = (y < ROWS) ? y : ROWS - 1;

    setCursor(true);
  }
}

/// <summary>
/// load default palette 
/// </summary>
inline __attribute__((always_inline))
void loadPalette() {
  // load default palette
  for (uint16_t c = 0; c < 256; c++) {
    display.setColor(c, rgb_to_brg(default_palette[c]));
  }

  // define cursor color
  display.setColor(IDX_CURSOR, rgb_to_brg(0xFFFFFF)); // visible (white)
}

/// <summary>
/// scroll up screen one line
/// </summary>
/// <param name="vLines"></param>
inline __attribute__((always_inline))
void cmdScrollUp() {
  memmove(display.getBuffer(), display.getBuffer() + WIDTH * FONT_CELL_HEIGHT, WIDTH * (HEIGHT - FONT_CELL_HEIGHT));
  display.fillRect(
    0,
    HEIGHT - FONT_CELL_HEIGHT,
    WIDTH,
    FONT_CELL_HEIGHT,
    currentBGColor);

  // shift text buffer up
  for (uint8_t r = 1; r < ROWS; r++) {
    memcpy(textBuffer[r - 1], textBuffer[r], COLS);
  }
  memset(textBuffer[ROWS - 1], ' ', COLS);
}

/// <summary>
/// 
/// </summary>
inline __attribute__((always_inline))
void cmdNewline() {
  gCursor.col = 0;

  if (++gCursor.row >= ROWS) {
    if (vduMode->autoScroll)
      cmdScrollUp();
    gCursor.row = ROWS - 1;
  }
}

/// <summary>
/// get a character from the text buffer
/// </summary>
/// <param name="x"></param>
/// <param name="y"></param>
/// <returns></returns>
uint8_t vduReadc(const uint16_t x, const uint16_t y) {

  if (vduMode->textMode) {
    uint16_t col = (x < COLS) ? x : COLS - 1;
    uint16_t row = (y < ROWS) ? y : ROWS - 1;

    return (textBuffer[row][col]);
  }
  else
    return 0x00;
}

/// <summary>
/// output a char
/// </summary>
/// <param name="c"></param>
/// <returns></returns>
void vduPutc(const uint8_t c) {
  if (! vduMode->textMode) {
    display.setColor(currentColor, currentBGColor);
    display.write(c);
    return;
  }

  setCursor(false);

  // process char
  switch (c) {
  case 0:
    break;
  case '\r':
    gCursor.col = 0;
    break;
  case '\n':
    if (vduMode->crlf)
      gCursor.col = 0;
    cmdNewline();
    break;
  case 0x08: // BS
  case 0x7F:
    if (gCursor.col > 0) {
      gCursor.col--;
    }
    break;
  default:   // regular char
    if ((uint8_t)c >= 0x20) {
      textBuffer[gCursor.row][gCursor.col] = c;

      if (c == ' ') {
        display.fillRect(
          gCursor.col * FONT_CELL_WIDTH,
          gCursor.row * FONT_CELL_HEIGHT,
          FONT_CELL_WIDTH,
          FONT_CELL_HEIGHT,
          currentBGColor
        );
      }
      else {
        display.setTextColor(currentColor, currentBGColor);
        display.setCursor(
          gCursor.col * FONT_CELL_WIDTH,
          gCursor.row * FONT_CELL_HEIGHT
        );

        display.write(c);
      }

      if (++gCursor.col >= COLS) {
        if (vduMode->textWrap)
          cmdNewline();
        else
          gCursor.col = COLS - 1;
      }
    }
    break;
  }

  setCursor(true);
}

/// <summary>
/// output a string
/// </summary>
/// <param name="str"></param>
void vduPrintStr(const char* str) {
  uint16_t len = strlen(str);

  for (int i = 0; i < len; i++) {
    vduPutc(str[i]);
  }
}

/// <summary>
/// output to screen ala printf
/// </summary>
/// <param name="fmt"></param>
/// <param name="..."></param>
void vduPrintf(char const* fmt, ...) {
  char buf[128];   // size is your choice
  va_list args;

  va_start(args, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len < 0) {
    return; // formatting error
  }

  if (len > (int)sizeof(buf)) {
    len = sizeof(buf); // truncated
  }

  for (int i = 0; i < len; i++) {
    vduPutc(buf[i]);
  }
}

/// <summary>
/// set mode of operation for VDU
/// </summary>
/// <param name="vMode"></param>
void vduSetMode(const uint8_t vMode) {
  vduMode = &vduModes[vMode % 8];
}

/// <summary>
/// clear display
/// </summary>
void cmdClearDisplay() {
  display.fillScreen(currentBGColor);
  display.setCursor(0, 0);
  gCursor.row = 0;
  gCursor.col = 0;
  gCursor.visible = false;

  initTextbuffer();

  setCursor(true);
}

/// <summary>
/// some default startup screen
/// </summary>
static void helloDisplay() {
  setTColor(YELLOW);

  vduPrintStr(" N  N          66  555   00   22\n");
  vduPrintStr(" N  N         6    5    0  0 2  2\n");
  vduPrintStr(" NN N         6    5    0  0    2\n");
  vduPrintStr(" N NN EEE  O  666  555  0  0   2   M   M M   M U   U\n");
  vduPrintStr(" N  N E   O O 6  6    5 0  0  2    MM MM MM MM U   U\n");
  vduPrintStr(" N  N EE  O O 6  6    5 0  0 2     M M M M M M U   U\n");
  vduPrintStr(" N  N E   O O 6  6 5  5 0  0 2     M   M M   M U  UU\n");
  vduPrintStr(" N  N EEE  O   66   55   00  2222  M   M M   M  UU U\n");

  setTColor(DEFAULT_COLOR);
}

/// <summary>
/// reset the vdu to a sane default state
/// </summary>
void resetDisplay(const uint8_t vMode) {
  setTColor(DEFAULT_COLOR);
  setBGColor(DEFAULT_BG_COLOR);

  // load default palette
  loadPalette();

  //  mem[VDU_CMD] = 0x00;

  vduSetMode(vMode % 8);        // Mode operations
  display.setFont();            // Use default font
  display.setTextWrap(vduMode->textWrap);
  display.setTextSize(1);       // Default size

  //  initSprites();
  //  initTiles(); // no tiles defined

  cmdClearDisplay();
}


/// <summary>
/// initialise to vdu
/// </summary>
void initVDU() {
  if (!display.begin()) {
    Serial1.println("*E: not enough RAM available");
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }

  resetDisplay(DEFAULT_MODE);

  helloDisplay();
}

/// <summary>
/// little task to blink cursor (optionally)
/// must be called regulary
/// </summary>
/// <param name="now_ms"></param>
void taskVDU() {
  static bool on = true;
  static uint32_t last = 0;
  uint32_t now_ms = millis();

  if (now_ms - last >= CURSOR_BLINK_INTERVAL_MS) {   // blink interval
    if (vduMode->blinkCursor) {
      display.setColor(
        IDX_CURSOR,
        on ? rgb_to_brg(default_palette[currentColor])
        : rgb_to_brg(default_palette[currentBGColor])
      );

      on = !on;
    }

    last = now_ms;
  }
}
