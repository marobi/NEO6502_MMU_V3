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

#define VERSION "3.17.687"

#define USE_VALIDATION 0

// USB keyboard locale default. Supported values:
//   USB_KEYBOARD_LOCALE_US
//   USB_KEYBOARD_LOCALE_DE
// Runtime monitor command: keymap [us|de]

#define USB_KEYBOARD_DEFAULT_LOCALE USB_KEYBOARD_LOCALE_US

// NEOX foreground-process break sequence, matched against raw HID reports
// before locale/ASCII translation.
//
// Defaults:
//   keycode $06 = HID Keyboard C
//   modifier-any mask $11 = left Ctrl ($01) or right Ctrl ($10)
//
// MODIFIER_ANY_MASK:
//   At least one configured bit must be present when nonzero.
//
// MODIFIER_ALL_MASK:
//   Every configured bit must be present when nonzero.
//
// Examples:
//   Ctrl-C (either Ctrl): key=$06, any=$11, all=$00
//   Ctrl-Shift-C:         key=$06, any=$11, all=$22
//   F12 without modifier: key=$45, any=$00, all=$00
#define NEOX_CONSOLE_BREAK_KEYCODE            0x06
#define NEOX_CONSOLE_BREAK_MODIFIER_ANY_MASK  0x11
#define NEOX_CONSOLE_BREAK_MODIFIER_ALL_MASK  0x00

// -------------------------------------------------------------------------------------

// Fixed RP2350 clock
constexpr uint32_t SYS_CLOCK_HZ       = 240 * MHZ;

constexpr uint32_t DEFAULT_6502_CLOCK = (4 * MHZ);        // 4 MHZ;

constexpr uint32_t RAM_SIZE           = (512ul * 1024ul); // RAM size;

#define RESOLUTION_640x480  1
#define WIDTH            640   // TBD display.width()
#define HEIGHT           480   // TBD display.height()
#define FONT_CHAR_WIDTH  7 
#define FONT_CHAR_HEIGHT 19
#define FONT_CELL_WIDTH  (FONT_CHAR_WIDTH + 1)
#define FONT_CELL_HEIGHT (FONT_CHAR_HEIGHT + 1)
#define ROWS             (HEIGHT / FONT_CELL_HEIGHT)
#define COLS             (WIDTH / FONT_CELL_WIDTH)

#define FONT_BASELINE_Y  19

