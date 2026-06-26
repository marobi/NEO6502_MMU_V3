#pragma once

#include <stdint.h>
#include "config.h"

// ============================================================
// usb_keyboard_layout.h
// NEO6502_MMU - USB boot-keyboard locale mapping
//
// Converts TinyUSB HID boot-keyboard usage IDs plus modifiers to the
// single-byte console input expected by monitorConsoleInput().
//
// The built-in layouts are table-driven. This is intentional: a later file
// loader can populate the same table structure from RP-owned storage without
// changing usb_hid_input.cpp.
//
// Current character scope is ASCII-safe. Locale-specific characters that do
// not yet have a defined NEOX 8-bit character encoding are mapped to 0.
// ============================================================

enum USBKeyboardLocale : uint8_t {
  USB_KEYBOARD_LOCALE_US = 0,
  USB_KEYBOARD_LOCALE_DE = 1,
  USB_KEYBOARD_LOCALE_CUSTOM = 255,
};

#ifndef USB_KEYBOARD_DEFAULT_LOCALE
#define USB_KEYBOARD_DEFAULT_LOCALE USB_KEYBOARD_LOCALE_US
#endif

static constexpr uint8_t USB_KEYBOARD_LAYOUT_KEY_COUNT = 128;
static constexpr uint8_t USB_KEYBOARD_LAYOUT_NAME_LEN = 8;

struct USBKeyboardLayoutTable {
  char name[USB_KEYBOARD_LAYOUT_NAME_LEN];
  uint8_t normal[USB_KEYBOARD_LAYOUT_KEY_COUNT];
  uint8_t shifted[USB_KEYBOARD_LAYOUT_KEY_COUNT];
  uint8_t ctrl[USB_KEYBOARD_LAYOUT_KEY_COUNT];
  uint8_t flags;
};

void usb_keyboard_set_locale(USBKeyboardLocale locale);
USBKeyboardLocale usb_keyboard_get_locale();
const char* usb_keyboard_get_locale_name();

const USBKeyboardLayoutTable* usb_keyboard_get_active_layout_table();
bool usb_keyboard_set_active_layout_table(const USBKeyboardLayoutTable* layout);

uint8_t usb_keyboard_keycode_to_ascii_locale(uint8_t keycode, uint8_t modifier);
