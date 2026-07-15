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
#include "VT100_80x24_8x20.h"
//#include "MacMonaco_80x24_8x20.h"
//#include "MacMono_80x24_8x20.h"
#include "config.h"
#include "palette.h"
#include "vdu.h"
#include "scheduler.h"

// NEO6502_MMU settings: resolution 320x240 by 256 colors, single buffer
DVHSTXPinout pinConfig = { 14, 18, 16, 12 };
#ifdef RESOLUTION_320x240
DVHSTX8 display(pinConfig, DVHSTX_RESOLUTION_320x240, false);
#endif
#ifdef RESOLUTION_640x480
DVHSTX8 display(pinConfig, DVHSTX_RESOLUTION_640x480, false);
#endif
// visibleCursor cursorShape blinkCursor textMode geoAspect autoScroll smoothScroll textWrap localEcho ucaseOnly  CRLF, SceenMode
static const vdu_mode_t vduModes[NUMBER_OF_MODES] = {
  {true,         cBLOCK,     true,       true,    false,    true,      true,        false,   false,    false,     true,  true  }, // Mode 0 text mode + block cursor + smoothscroll + sceen mode
  {true,         cUNDERLINE, true,       true,    false,    true,      true,        false,   false,    false,     true,  true  }, // Mode 1 text model + underline cursor + smoothscroll + sceen mode
  {true,         cBLOCK,     true,       true,    false,    true,      false,       false,   false,    false,     true,  false }, // Mode 0 text mode + block cursor
  {true,         cUNDERLINE, true,       true,    false,    true,      false,       false,   false,    false,     true,  false }, // Mode 1 text model + underline cursor
  {false,        cUNDERLINE, false,      true,    false,    true,      false,       false,   false,    false,     true,  false }, // Mode 2 text mode no cursor
  {false,        cUNDERLINE, false,      true,    false,    false,     false,       false,   false,    false,     true,  false }, // Mode 3 text mode no cursor no scroll
  {false,        cUNDERLINE, false,      false,   false,    false,     false,       false,   false,    false,     true,  false }, // Mode 4 text graphics mode no cursor no scroll
  {true,         cBLOCK,     true,       true,    true,     true,      false,       false,   false,    false,     true,  false }, // Mode 5 = mode 0 + geo aspect correction
  {false,        cUNDERLINE, false,      false,   true,     false,     false,       false,   false,    false,     true,  false }, // Mode 6 = mode 4 + geo aspect correction
  {false,        cUNDERLINE, false,      true,    true,     false,     false,       false,   false,    false,     true,  false }, // Mode 7 = mode 3 + geo aspect correction
};

/// <summary>
/// vdu mode definition (see vduModes array for details)
/// </summary>
const vdu_mode_t* vduMode = &vduModes[DEFAULT_MODE];

/// <summary>
/// cursor definition
/// </summary>
typedef struct {
  uint8_t col;
  uint8_t row;
  cursor_shape_t shape;
  bool enabled;     // cursor allowed in this mode
  bool drawn;       // cursor currently rendered
  bool blink_on;
} cursor_def_t;

// our output text cursor
static cursor_def_t   gCursor;        // our text cursor
static cursor_def_t   gSaveCursor;    // our saved text cursor

/// <summary>
/// vdu cell definition: character + foreground color + background color
/// </summary>
typedef struct {
  uint8_t ch;
  uint8_t fg;
  uint8_t bg;
} vdu_cell_t;

static vdu_cell_t     gScreen[ROWS][COLS];
static bool           gInEditmode = false;

static uint8_t        currentColor = DEFAULT_COLOR;
static uint8_t        currentBGColor = DEFAULT_BG_COLOR;
static bool           gInsertMode = false;
static bool           gAsScreenMode = false;

static bool           smoothScroll = false;
static bool           scrollActive = false;
static uint8_t        scrollPixel = 0;
static int            pendingCursorRow = -1;

#ifndef VDU_MOUSE_ENABLED_DEFAULT
#define VDU_MOUSE_ENABLED_DEFAULT 1
#endif

static constexpr int16_t VDU_MOUSE_POINTER_WIDTH = 10;
static constexpr int16_t VDU_MOUSE_POINTER_HEIGHT = 16;
static constexpr uint8_t VDU_MOUSE_POINTER_COLOR = WHITE;
static constexpr uint8_t VDU_MOUSE_LEFT_BUTTON = 0x01;

static constexpr uint16_t VDU_MOUSE_POINTER_BITMAP[VDU_MOUSE_POINTER_HEIGHT] = {
  0b1000000000,
  0b1100000000,
  0b1110000000,
  0b1111000000,
  0b1111100000,
  0b1111110000,
  0b1111111000,
  0b1111111100,
  0b1111111110,
  0b1111100000,
  0b1101110000,
  0b1000110000,
  0b0000111000,
  0b0000011000,
  0b0000011100,
  0b0000000000,
};

typedef struct {
  bool enabled;
  bool active;
  bool drawn;
  int16_t x;
  int16_t y;
  uint8_t buttons;
} vdu_mouse_def_t;

static vdu_mouse_def_t gMouse = {
  VDU_MOUSE_ENABLED_DEFAULT != 0,
  false,
  false,
  WIDTH / 2,
  HEIGHT / 2,
  0
};

static uint8_t gMouseVduUpdateDepth = 0;

static void showCursor();
static void vduMouseHideOverlay();
static void vduMouseDrawOverlay();
static void vduMouseHideForVduUpdate();
static void vduMouseShowAfterVduUpdate();

/// <summary>
/// convert column number to pixel X coordinate
/// </summary>
/// <param name="col"></param>
/// <returns></returns>
static inline uint16_t cellPixelX(const uint8_t col) {
  return col * FONT_CELL_WIDTH;
}

/// <summary>
/// convert row number to pixel Y coordinate
/// </summary>
/// <param name="row"></param>
/// <returns></returns>
static inline uint16_t cellPixelY(const uint8_t row) {
  return row * FONT_CELL_HEIGHT;
}

/// <summary>
/// celculate baseline Y coordinate for a given row (for text rendering)
/// </summary>
/// <param name="row"></param>
/// <returns></returns>
static inline uint16_t cellBaselineY(const uint8_t row) {
  return cellPixelY(row) + FONT_BASELINE_Y;
}

/// <summary>
/// helper routine to convert RGB to BRG
/// </summary>
/// <param name="rgb"></param>
/// <returns></returns>
static uint32_t rgb_to_brg(uint32_t rgb) {
  uint32_t r = (rgb >> 16) & 0xFF;
  uint32_t g = (rgb >> 8) & 0xFF;
  uint32_t b = rgb & 0xFF;

  return (b << 16) | (r << 8) | g;
}

/// <summary>
/// load default palette 
/// </summary>
static void loadPalette() {
  // load default palette
  for (uint16_t c = 0; c < 256; c++) {
    display.setColor(c, rgb_to_brg(default_palette[c]));
  }

  display.setColor(IDX_CURSOR_BG, rgb_to_brg(currentBGColor));
  display.setColor(IDX_CURSOR_FG, rgb_to_brg(currentColor));
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
bool getAsScreenMode() {
  return gAsScreenMode;
}

/// <summary>
/// 
/// </summary>
/// <param name="edit"></param>
void setAsScreenMode(bool mode) {
  gAsScreenMode = mode;
}

/// <summary>
/// set mode of operation for VDU
/// </summary>
/// <param name="vMode"></param>
void vduSetMode(const uint8_t vMode) {
  vduMouseHideForVduUpdate();

  vduMode = &vduModes[vMode % NUMBER_OF_MODES];

  setAsScreenMode(vduMode->screenMode);
  smoothScroll = vduMode->smoothScroll;

  gCursor.shape = vduMode->cursorShape;
  gCursor.enabled = vduMode->visibleCursor;
  gCursor.blink_on = vduMode->blinkCursor;

  vduMouseShowAfterVduUpdate();
}

/// <summary>
/// 
/// </summary>
/// <param name="on"></param>
void vduSetSmoothscroll(const bool on) {
  smoothScroll = on;
}

/// <summary>
/// helper to init text buffer
/// </summary>
/// <param name=""></param>
static void initTextbuffer() {
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      gScreen[r][c].ch = ' ';
      gScreen[r][c].fg = currentColor;
      gScreen[r][c].bg = currentBGColor;
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
/// 
/// </summary>
/// <param name="col"></param>
/// <param name="row"></param>
static void vduDrawCell(const uint8_t col, const uint8_t row) {
  vdu_cell_t c = gScreen[row][col];

  uint16_t x = col * FONT_CELL_WIDTH;
  uint16_t y = row * FONT_CELL_HEIGHT;

  display.fillRect(
    x,
    y,
    FONT_CELL_WIDTH,
    FONT_CELL_HEIGHT,
    c.bg
  );

  if (c.ch != ' ') {
    display.setTextColor(c.fg, c.bg);
    display.setCursor(x, y + FONT_BASELINE_Y);
    display.write(c.ch);
  }
}

/// <summary>
/// vduDrawCellPreservingCursor redraws a text cell from gScreen, but redraws the
/// current cursor cell using the already-active cursor palette indexes when the
/// cursor is currently rendered. This lets the cursor blink task remain the sole
/// owner of the cursor blink palette phase while mouse overlay cleanup redraws
/// cells underneath the pointer.
/// </summary>
/// <param name="col">text cell column.</param>
/// <param name="row">text cell row.</param>
static void vduDrawCellPreservingCursor(const uint8_t col, const uint8_t row) {
  if (!gCursor.drawn || col != gCursor.col || row != gCursor.row) {
    vduDrawCell(col, row);
    return;
  }

  vdu_cell_t cell = gScreen[row][col];

  uint16_t x = col * FONT_CELL_WIDTH;
  uint16_t y = row * FONT_CELL_HEIGHT;

  switch (gCursor.shape) {
  case cBLOCK:
    display.setTextColor(IDX_CURSOR_FG, IDX_CURSOR_BG);
    display.fillRect(
      x,
      y,
      FONT_CELL_WIDTH,
      FONT_CELL_HEIGHT,
      IDX_CURSOR_BG
    );

    if (cell.ch != ' ') {
      display.setCursor(x, y + FONT_BASELINE_Y);
      display.write(cell.ch);
    }
    break;

  case cUNDERLINE:
    vduDrawCell(col, row);
    display.fillRect(
      x,
      y + FONT_CELL_HEIGHT - 2,
      FONT_CELL_WIDTH,
      2,
      IDX_CURSOR_BG
    );
    break;
  }
}

/// <summary>
/// vduMouseClampCoordinate clamps a mouse coordinate to the visible VDU area.
/// </summary>
/// <param name="value">candidate coordinate.</param>
/// <param name="maximum">maximum inclusive coordinate.</param>
/// <returns>clamped coordinate.</returns>
static int16_t vduMouseClampCoordinate(const int32_t value, const int16_t maximum) {
  if (value < 0)
    return 0;

  if (value > maximum)
    return maximum;

  return (int16_t)value;
}

/// <summary>
/// vduMouseDrawPixel draws one mouse overlay pixel when it is inside the visible area.
/// </summary>
/// <param name="x">pixel X coordinate.</param>
/// <param name="y">pixel Y coordinate.</param>
/// <param name="color">palette color index.</param>
static void vduMouseDrawPixel(const int16_t x, const int16_t y, const uint8_t color) {
  if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT)
    return;

  display.drawPixel(x, y, color);
}

/// <summary>
/// vduMouseRedrawUnderlyingText redraws the text cells underneath the mouse overlay.
/// </summary>
static void vduMouseRedrawUnderlyingText() {
  if (!vduMode->textMode)
    return;

  int16_t x0 = gMouse.x;
  int16_t y0 = gMouse.y;
  int16_t x1 = gMouse.x + VDU_MOUSE_POINTER_WIDTH - 1;
  int16_t y1 = gMouse.y + VDU_MOUSE_POINTER_HEIGHT - 1;

  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 >= WIDTH)
    x1 = WIDTH - 1;
  if (y1 >= HEIGHT)
    y1 = HEIGHT - 1;

  uint8_t col0 = x0 / FONT_CELL_WIDTH;
  uint8_t row0 = y0 / FONT_CELL_HEIGHT;
  uint8_t col1 = x1 / FONT_CELL_WIDTH;
  uint8_t row1 = y1 / FONT_CELL_HEIGHT;

  if (col1 >= COLS)
    col1 = COLS - 1;
  if (row1 >= ROWS)
    row1 = ROWS - 1;

  for (uint8_t row = row0; row <= row1; row++) {
    for (uint8_t col = col0; col <= col1; col++) {
      vduDrawCellPreservingCursor(col, row);
    }
  }
}

/// <summary>
/// vduMouseHideOverlay removes the software mouse overlay by redrawing the underlying text cells.
/// </summary>
static void vduMouseHideOverlay() {
  if (!gMouse.drawn)
    return;

  vduMouseRedrawUnderlyingText();
  gMouse.drawn = false;
}

/// <summary>
/// vduMouseDrawOverlay draws the RP-local mouse pointer last. The overlay only draws in text modes
/// because text cells are the available authoritative underlay. The overlay is suppressed
/// while smooth scrolling is active because the partially shifted framebuffer no longer
/// matches the stable gScreen cell grid until the scroll completes.
/// </summary>
static void vduMouseDrawOverlay() {
  if (!gMouse.enabled || !gMouse.active || gMouse.drawn || !vduMode->textMode || scrollActive)
    return;

  for (int16_t row = 0; row < VDU_MOUSE_POINTER_HEIGHT; row++) {
    uint16_t const bits = VDU_MOUSE_POINTER_BITMAP[row];

    for (int16_t col = 0; col < VDU_MOUSE_POINTER_WIDTH; col++) {
      if ((bits & (uint16_t)(1u << (VDU_MOUSE_POINTER_WIDTH - 1 - col))) == 0)
        continue;

      vduMouseDrawPixel(gMouse.x + col, gMouse.y + row, VDU_MOUSE_POINTER_COLOR);
    }
  }

  gMouse.drawn = true;
}

/// <summary>
/// vduMouseHideForVduUpdate hides the mouse overlay before normal VDU drawing.
/// Nested calls are allowed.
/// </summary>
static void vduMouseHideForVduUpdate() {
  if (gMouseVduUpdateDepth++ == 0)
    vduMouseHideOverlay();
}

/// <summary>
/// vduMouseShowAfterVduUpdate restores the mouse overlay after normal VDU drawing.
/// Nested calls are allowed.
/// </summary>
static void vduMouseShowAfterVduUpdate() {
  if (gMouseVduUpdateDepth == 0)
    return;

  gMouseVduUpdateDepth--;

  if (gMouseVduUpdateDepth == 0)
    vduMouseDrawOverlay();
}

/// <summary>
/// Enable or disable the RP-side VDU mouse overlay. This is VDU-local only and
/// does not expose mouse state to the 6502.
/// </summary>
/// <param name="enabled">true to show the overlay, false to hide it.</param>
void vduMouseEnable(const bool enabled) {
  vduMouseHideForVduUpdate();
  gMouse.enabled = enabled;
  vduMouseShowAfterVduUpdate();
}

/// <summary>
/// Return whether the RP-side VDU mouse overlay is enabled.
/// </summary>
/// <returns>true when enabled.</returns>
bool vduMouseIsEnabled() {
  return gMouse.enabled;
}

/// <summary>
/// Update the RP-side VDU mouse overlay from a relative USB HID mouse report.
/// V2 remains RP-local: it does not report mouse state to the 6502. A left-button
/// press edge in text mode moves the existing VDU text cursor to the mouse cell.
/// </summary>
/// <param name="dx">relative X movement from the HID report.</param>
/// <param name="dy">relative Y movement from the HID report.</param>
/// <param name="buttons">current HID button bitmask.</param>
void vduMouseUpdate(const int8_t dx, const int8_t dy, const uint8_t buttons) {
  bool const movement = (dx != 0) || (dy != 0);
  bool const buttonsChanged = (buttons != gMouse.buttons);
  bool const leftClick = ((buttons & VDU_MOUSE_LEFT_BUTTON) != 0) &&
                         ((gMouse.buttons & VDU_MOUSE_LEFT_BUTTON) == 0);

  if (!movement && !buttonsChanged)
    return;

  vduMouseHideForVduUpdate();

  gMouse.active = true;

  if (movement) {
    gMouse.x = vduMouseClampCoordinate((int32_t)gMouse.x + dx, WIDTH - 1);
    gMouse.y = vduMouseClampCoordinate((int32_t)gMouse.y + dy, HEIGHT - 1);
  }

  gMouse.buttons = buttons;

  if (leftClick && gMouse.enabled && vduMode->textMode) {
    uint16_t const col = (uint16_t)(gMouse.x / FONT_CELL_WIDTH);
    uint16_t const row = (uint16_t)(gMouse.y / FONT_CELL_HEIGHT);
    moveCursor(col, row);
  }

  vduMouseShowAfterVduUpdate();
}

/// <summary>
/// show cursor 
/// </summary>
static void showCursor() {
  if (!gCursor.enabled || gCursor.drawn)
    return;

  vdu_cell_t cell = gScreen[gCursor.row][gCursor.col];

  uint16_t x = gCursor.col * FONT_CELL_WIDTH;
  uint16_t y = gCursor.row * FONT_CELL_HEIGHT;

  display.setColor(
    IDX_CURSOR_BG,
    rgb_to_brg(default_palette[cell.bg])
  );

  display.setColor(
    IDX_CURSOR_FG,
    rgb_to_brg(default_palette[cell.fg])
  );

  switch (gCursor.shape) {
  case cBLOCK:
    display.setTextColor(IDX_CURSOR_FG, IDX_CURSOR_BG);
    display.fillRect(
      x,
      y,
      FONT_CELL_WIDTH,
      FONT_CELL_HEIGHT - 2,       // uglh
      IDX_CURSOR_BG
    );

    if (cell.ch != ' ') {
      display.setTextColor(IDX_CURSOR_FG, IDX_CURSOR_BG);
      display.setCursor(x, y + FONT_BASELINE_Y);
      display.write(cell.ch);
    }
    break;

  case cUNDERLINE:
    display.setTextColor(IDX_CURSOR_FG, IDX_CURSOR_BG);
    display.fillRect(
      x,
      y + FONT_CELL_HEIGHT - 2,
      FONT_CELL_WIDTH,
      2,
      IDX_CURSOR_BG
    );
    break;
  }

  gCursor.drawn = true;
}

/// <summary>
/// hide cursor
/// </summary>
static void hideCursor() {
  if (!gCursor.drawn)
    return;

  vdu_cell_t cell = gScreen[gCursor.row][gCursor.col];

  // restore original colors
  display.setColor(
    IDX_CURSOR_BG,
    rgb_to_brg(default_palette[cell.bg])
  );

  display.setColor(
    IDX_CURSOR_FG,
    rgb_to_brg(default_palette[cell.fg])
  );

  vduDrawCell(gCursor.col, gCursor.row);

  gCursor.drawn = false;
}

/// <summary>
/// alter cursor shape (block/underline)
/// </summary>
/// <param name="vShape"></param>
void alterCursor(const cursor_shape_t vShape) {
  if (gCursor.shape != vShape) {
    gCursor.shape = vShape;
    Serial1.printf("*D: alterCursor [%02X]\n", vShape);
    if (gCursor.enabled) {
      hideCursor();
      showCursor();
    }
  }
}

/// <summary>
/// control show/hide cursor
/// </summary>
/// <param name="vVisible"></param>
void setCursor(const boolean vShow) {
  vduMouseHideForVduUpdate();

  if (vduMode->textMode) {
    gCursor.enabled = vduMode->visibleCursor;

    if (vShow)
      showCursor();
    else
      hideCursor();
  }

  vduMouseShowAfterVduUpdate();
}

/// <summary>
/// move cursor
/// </summary>
/// <param name="x"></param>
/// <param name="y"></param>
void moveCursor(const uint16_t x, const uint16_t y) {
  vduMouseHideForVduUpdate();

  bool wasVisible = gCursor.drawn;

  if (wasVisible)
    hideCursor();

  gCursor.col = (x < COLS) ? x : COLS - 1;
  gCursor.row = (y < ROWS) ? y : ROWS - 1;

  if (wasVisible)
    showCursor();

  vduMouseShowAfterVduUpdate();
}

/// <summary>
/// vdu redraw a text line (used after insert/delete char to redraw the line from text buffer)
/// </summary>
/// <param name="row"></param>
static void vduRedrawLine(uint8_t row) {
  uint16_t y = row * FONT_CELL_HEIGHT;

  /* clear the full text line once */
  display.fillRect(
    0,
    y,
    WIDTH,
    FONT_CELL_HEIGHT,
    currentBGColor
  );

  for (int col = 0; col < COLS; col++) {
    vdu_cell_t* cell = &gScreen[row][col];

    if (cell->ch != ' ') {
      display.setTextColor(cell->fg, cell->bg);

      display.setCursor(
        col * FONT_CELL_WIDTH,
        y + FONT_BASELINE_Y
      );

      display.write(cell->ch);
    }
  }
}


/// <summary>
/// 
/// </summary>
static void scrollStep() {
  vduMouseHideForVduUpdate();

  uint8_t* fb = display.getBuffer();

  // move framebuffer up one scanline
  memmove(fb, fb + WIDTH, WIDTH * (HEIGHT - 1));

  // clear new bottom scanline
  memset(fb + WIDTH * (HEIGHT - 1), currentBGColor, WIDTH);

  scrollPixel++;

  if (scrollPixel == FONT_CELL_HEIGHT) {
    scrollPixel = 0;
    scrollActive = false;

    // rotate text buffer
    for (uint8_t r = 1; r < ROWS; r++)
      memcpy(gScreen[r - 1], gScreen[r], COLS * sizeof(vdu_cell_t));

    // clear bottom line
    for (uint8_t c = 0; c < COLS; c++) {
      gScreen[ROWS - 1][c].ch = ' ';
      gScreen[ROWS - 1][c].fg = currentColor;
      gScreen[ROWS - 1][c].bg = currentBGColor;
    }

    // redraw the full bottom row
    if (pendingCursorRow >= 0) {
      gCursor.row = pendingCursorRow;
      pendingCursorRow = -1;
    }

    vduRedrawLine(ROWS - 1);

    /* force cursor redraw after scroll */
    gCursor.drawn = false;
    showCursor();
  }

  vduMouseShowAfterVduUpdate();
}

/// <summary>
/// scroll up screen one line
/// </summary>
/// <param name="vLines"></param>
static void cmdScrollUp() {
  vduMouseHideForVduUpdate();

  if (gCursor.drawn) {
    hideCursor();
  }

  /* smooth pixel scrolling */
  if (smoothScroll) {
    scrollPixel = 0;
    scrollActive = true;
    vduMouseShowAfterVduUpdate();
    return;
  }

  /* ----- instant scroll ----- */

  uint8_t* fb = display.getBuffer();

  /* move framebuffer up one text row */
  memmove(
    fb,
    fb + WIDTH * FONT_CELL_HEIGHT,
    WIDTH * (HEIGHT - FONT_CELL_HEIGHT)
  );

  /* clear new bottom area */
  memset(
    fb + WIDTH * (HEIGHT - FONT_CELL_HEIGHT),
    currentBGColor,
    WIDTH * FONT_CELL_HEIGHT
  );

  /* rotate text buffer */
  for (uint8_t r = 1; r < ROWS; r++)
    memcpy(gScreen[r - 1], gScreen[r], COLS * sizeof(vdu_cell_t));

  /* clear bottom row */
  for (uint8_t c = 0; c < COLS; c++) {
    gScreen[ROWS - 1][c].ch = ' ';
    gScreen[ROWS - 1][c].fg = currentColor;
    gScreen[ROWS - 1][c].bg = currentBGColor;
  }

  /* update cursor row if pending */
  if (pendingCursorRow >= 0) {
    gCursor.row = pendingCursorRow;
    pendingCursorRow = -1;
  }

  /* redraw bottom line */
  vduRedrawLine(ROWS - 1);

  gCursor.drawn = false;
  showCursor();

  vduMouseShowAfterVduUpdate();
}

/// <summary>
/// vdu save cursor position
/// </summary>
void vduSaveCursor() {
  gSaveCursor = gCursor;
  gInEditmode = true;
}

/// <summary>
/// vdu restore cursor position at COL = 0
/// taking care of scrolling
/// </summary>
void vduRestoreCursor() {
  if (gInEditmode) {
    if (gSaveCursor.row == gCursor.row) {
      if (gSaveCursor.row == (ROWS - 1)) {
        hideCursor();
        cmdScrollUp();
        moveCursor(0, ROWS - 1);
        showCursor();
      }
      else
        moveCursor(0, gSaveCursor.row + 1);
    }
    else
      moveCursor(0, gSaveCursor.row);

    gInEditmode = false;
  }
}

/// <summary>
/// 
/// </summary>
static void cmdNewline() {
  gCursor.col = 0;

  if (++gCursor.row >= ROWS) {
    if (vduMode->autoScroll) {
      pendingCursorRow = ROWS - 1;
      cmdScrollUp();
    }
    else {
      gCursor.row = ROWS - 1;
    }
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

    return (gScreen[row][col].ch);
  }
  else
    return 0x00;
}

/// <summary>
/// show a space at cursor position
/// </summary>
/// <param name="col"></param>
/// <param name="row"></param>
static void vduDisplayClearcell(const int col, const int row) {
  display.fillRect(
    col * FONT_CELL_WIDTH,
    row * FONT_CELL_HEIGHT,
    FONT_CELL_WIDTH,
    FONT_CELL_HEIGHT,
    currentBGColor
  );
}


/// <summary>
/// vdu delete char at cursor position, rest of line move left, last char on line = space
/// </summary>
static void vduDisplayDeletechar() {
  uint8_t* fb = display.getBuffer();

  int pixel_x = gCursor.col * FONT_CELL_WIDTH;
  int pixel_y = gCursor.row * FONT_CELL_HEIGHT;

  uint8_t* src = fb + pixel_y * WIDTH + pixel_x + FONT_CELL_WIDTH;
  uint8_t* dst = fb + pixel_y * WIDTH + pixel_x;

  int move_width = (COLS - gCursor.col - 1) * FONT_CELL_WIDTH;

  for (int y = 0; y < FONT_CELL_HEIGHT; y++) {
    memmove(dst, src, move_width);

    src += WIDTH;
    dst += WIDTH;
  }

  vduDisplayClearcell(COLS - 1, gCursor.row);
}

/// <summary>
/// vdu insert space at cursor position, rest of line move right, last char on line lost
/// </summary>
/// <param name="c"></param>
static void vduDisplayInsertspace() {
  if (gCursor.col >= COLS - 1)
    return;

  uint8_t* fb = display.getBuffer();

  int pixel_x = gCursor.col * FONT_CELL_WIDTH;
  int pixel_y = gCursor.row * FONT_CELL_HEIGHT;

  uint8_t* src = fb + pixel_y * WIDTH + pixel_x;
  uint8_t* dst = src + FONT_CELL_WIDTH;

  int move_width = (COLS - gCursor.col - 1) * FONT_CELL_WIDTH;

  for (int y = 0; y < FONT_CELL_HEIGHT; y++)
  {
    memmove(dst, src, move_width);

    src += WIDTH;
    dst += WIDTH;
  }

  vduDisplayClearcell(gCursor.col, gCursor.row);
}

/// <summary>
/// vdu delete char at cursor position, rest of line move left, last char on line lost
/// </summary>
static void vduDeletec() {
  vdu_cell_t* line = gScreen[gCursor.row];

  memmove(
    &line[gCursor.col],
    &line[gCursor.col + 1],
    (COLS - gCursor.col - 1) * sizeof(vdu_cell_t)
  );

  line[COLS - 1].ch = ' ';
  line[COLS - 1].fg = currentColor;
  line[COLS - 1].bg = currentBGColor;

  vduRedrawLine(gCursor.row);
}

/// <summary>
/// vdu insert char at cursor position, rest of line move right, last char on line lost
/// </summary>
/// <param name="c"></param>
static void vduInsertc(uint8_t c) {
  vdu_cell_t* line = gScreen[gCursor.row];

  memmove(
    &line[gCursor.col + 1],
    &line[gCursor.col],
    (COLS - gCursor.col - 1) * sizeof(vdu_cell_t)
  );

  line[gCursor.col].ch = c;
  line[gCursor.col].fg = currentColor;
  line[gCursor.col].bg = currentBGColor;

  vduRedrawLine(gCursor.row);
}

/// <summary>
/// vdu display char at cursor position, update text buffer, move cursor right, scroll if needed
/// </summary>
/// <param name="c"></param>
static void vduDisplayc(const uint8_t c) {
  if (c >= 0x20) {
    uint16_t x = gCursor.col * FONT_CELL_WIDTH;
    uint16_t y = gCursor.row * FONT_CELL_HEIGHT;

    gScreen[gCursor.row][gCursor.col].ch = c;
    gScreen[gCursor.row][gCursor.col].fg = currentColor;
    gScreen[gCursor.row][gCursor.col].bg = currentBGColor;

    vduDisplayClearcell(gCursor.col, gCursor.row);

    if (c != 0x20) {
      display.setTextColor(currentColor, currentBGColor);
      display.setCursor(x, y + FONT_BASELINE_Y);
      display.write(c);
    }
  }
}

//-----------------------------------------------------------------------------------------
static void cmdCursorLeft() {
  if (gCursor.col > 0)
    gCursor.col--;
}

static void cmdCursorRight() {
  if (gCursor.col < COLS - 1)
    gCursor.col++;
}

static void cmdCursorUp() {
  if (gCursor.row > 0)
    gCursor.row--;
}

static void cmdCursorDown() {
  if (gCursor.row < ROWS - 1)
    gCursor.row++;
}

static void cmdCursorBOL() {
  gCursor.col = 0;
}

static void cmdCursorEOL() {
  for (int c = COLS - 1; c >= 0; c--) {
    if (gScreen[gCursor.row][c].ch != ' ') {
      gCursor.col = c + 1;
      if (gCursor.col >= COLS)
        gCursor.col = COLS - 1;
      return;
    }
  }
  gCursor.col = 0;
}

static void cmdDelete() {
  vduDisplayDeletechar();
  vduDeletec();
}

static void cmdClearEOL() {
  for (int c = gCursor.col; c < COLS; c++) {
    gScreen[gCursor.row][c].ch = ' ';
    gScreen[gCursor.row][c].fg = currentColor;
    gScreen[gCursor.row][c].bg = currentBGColor;

    vduDisplayClearcell(c, gCursor.row);
  }
}

static void cmdInsertMode() {
  gInsertMode = true;
}

static void cmdOverwriteMode() {
  gInsertMode = false;
}

static void cmdSmoothScroll() {
  vduSetSmoothscroll(true);
}

static void cmdInstantScroll() {
  vduSetSmoothscroll(false);
}

void cmdClearScreen()
{
  vduMouseHideForVduUpdate();

  // hide cursor during redraw
  hideCursor();

  // clear framebuffer
  display.fillScreen(currentBGColor);

  // reset text buffer
  initTextbuffer();

  display.setColor(
    IDX_CURSOR_BG,
    rgb_to_brg(default_palette[currentBGColor])
  );

  display.setColor(
    IDX_CURSOR_FG,
    rgb_to_brg(default_palette[currentColor])
  );

  // reset cursor position
  moveCursor(0, 0);

  // show cursor again if enabled
  setCursor(true);

  vduMouseShowAfterVduUpdate();
}

//-----------------------------------------------------------------------------------------

/// <summary>
/// vdu command function pointer type
/// </summary>
typedef void (*vdu_cmd_t)(void);

static const vdu_cmd_t ctrlTable[32] = {
    NULL,            // 0x00
    cmdCursorBOL,    // ^A
    cmdCursorLeft,   // ^B
    NULL,            // ^C
    cmdDelete,       // ^D
    cmdCursorEOL,    // ^E
    cmdCursorRight,  // ^F
    NULL,            // ^G  TBI: beep
    NULL,            // ^H  (backspace)
    cmdInsertMode,   // ^I
    NULL,            // ^J  LF
    cmdClearEOL,     // ^K
    cmdClearScreen,  // ^L
    NULL,            // ^M  CR
    cmdCursorDown,   // ^N
    cmdOverwriteMode,// ^O
    cmdCursorUp,     // ^P
    NULL,            // ^Q
    NULL,            // ^R
    NULL,            // ^S
    cmdInstantScroll,// ^T
    cmdSmoothScroll, // ^U
    NULL,            // ^V
    NULL,            // ^W
    NULL,            // ^X
    NULL,            // ^Y
    NULL             // ^Z  (return to monitor)
};

//-----------------------------------------------------------------------------------------
/// <summary>
/// output a char at cursor position
/// </summary>
/// <param name="c"></param>
/// <returns></returns>
void vduPutc(const uint8_t c) {
  while (scrollActive)
    taskVDU();

  vduMouseHideForVduUpdate();

  if (!vduMode->textMode) {
    display.setColor(currentColor, currentBGColor);
    display.write(c);
    vduMouseShowAfterVduUpdate();
    return;
  }

  setCursor(false);

  // process char
  switch (c) {
  case 0:
    break;

  case '\r':  // CR
    cmdNewline();
    vduSaveCursor();
//    Serial1.println("VDU: CR");
    break;

  case '\n':  // LF
    cmdNewline();
    vduSaveCursor();
//    Serial1.println("VDU: LF");
    break;

  case 0x08: // BS
  case 0x7F:
    if (gCursor.col > 0) {
      gCursor.col--;
    }
    break;

  default:   // regular char
    if ((c < 0x20) && gAsScreenMode) {  // control char in as-screen mode
      vdu_cmd_t lCmd = ctrlTable[c];
      if (lCmd) {
        lCmd();
      }
    }
    else {
      if (gInsertMode && gAsScreenMode) {  // insert char mode only in as-screen mode
        vduDisplayInsertspace();
        vduInsertc(c);
      }

      // ------------------------------------
      // at last output it
      vduDisplayc(c);

      // move cursor right if possible or wrap to new line if enabled
      if (++gCursor.col >= COLS) {
        if (vduMode->textWrap)    // auto wrap to new line
          cmdNewline();
        else
          gCursor.col = COLS - 1; // stay at end of line
      }
    }

    break;
  }

  setCursor(true);

  vduMouseShowAfterVduUpdate();
}

/// <summary>
/// vdu get a screen line trailing spaces removed
/// </summary>
/// <param name="row"></param>
/// <param name="Buffer"></param>
void vduGetScreenline(const uint8_t row, uint8_t* buffer)
{
  int end = COLS - 1;

  // find last non-space
  while (end >= 0 && gScreen[row][end].ch == ' ')
    end--;

  if (end < 0) {
    buffer[0] = 0;     // empty string
    return;
  }

  for (uint8_t i = 0; i <= end; i++)
    buffer[i] = gScreen[row][i].ch;

  buffer[end + 1] = 0x00;
}

/// <summary>
/// vdu get current screen line trailing spaces removed
/// </summary>
/// <param name="buffer"></param>
void vduGetCurrentScreenline(uint8_t* buffer) {
  vduGetScreenline(gCursor.row, buffer);
}

/// <summary>
/// output a string
/// </summary>
/// <param name="str"></param>
void vduPrintStr(const char* str) {
  while (*str)
    vduPutc(*str++);
}

/// <summary>
/// 
/// </summary>
/// <param name="vBuffer"></param>
/// <param name="vLength"></param>
uint16_t vduPrintBuf(const uint8_t* vBuffer, const uint16_t vLength) {
  for (uint16_t c = 0; c < vLength; c++)
    vduPutc(vBuffer[c]);

  return vLength;
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
    len = sizeof(buf) - 1; // truncated
  }

  for (int i = 0; i < len; i++) {
    vduPutc(buf[i]);
  }
}

/// <summary>
/// some default startup screen
/// </summary>
static void helloDisplay() {
  setTColor(YELLOW);

  vduPrintStr("\n");
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

  vduSetMode(vMode);                  // VDU mode (cursor shape, auto scroll, text wrap, etc)
  display.setFont(&VT100_80x24_8x20); // Use vt100 inspired font
//  display.setFont(&MacMonaco_80x24_8x20);
//  display.setFont(&MacMono_80x24_8x20);
  display.setTextWrap(vduMode->textWrap);
  display.setTextSize(1);            // Default size

  //  initSprites();
  //  initTiles();                   // no tiles defined

  cmdClearScreen();
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

//  helloDisplay();
}

/// <summary>
/// little task to blink cursor (optionally)
/// must be called regulary
/// </summary>
/// <param name="now_ms"></param>
void taskVDU() {
  static bool reverse = false;
  static uint32_t last = 0;
  static uint32_t lastScroll = 0;

  uint32_t now = millis();

  if (scrollActive && (now - lastScroll) >= SCROLL_INTERVAL_MS) {
    scrollStep();
    lastScroll = now;
  }

  if (scrollActive)
    return;

  if (now - last >= CURSOR_BLINK_INTERVAL_MS) {
    if (vduMode->blinkCursor && gCursor.drawn) {
      vdu_cell_t cell = gScreen[gCursor.row][gCursor.col];

      uint8_t fg = reverse ? cell.bg : cell.fg;
      uint8_t bg = reverse ? cell.fg : cell.bg;

      display.setColor(
        IDX_CURSOR_BG,
        rgb_to_brg(default_palette[bg])
      );

      display.setColor(
        IDX_CURSOR_FG,
        rgb_to_brg(default_palette[fg])
      );

      reverse = !reverse;
    }

    last = now;
  }
}
