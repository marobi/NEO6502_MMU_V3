#pragma once

#define VERSION "3.0.7"

constexpr auto DEFAULT_6502_CLOCK = (1000000L);       // 1 MHZ;

constexpr uint32_t RAM_SIZE = (512 * 1024 * 1024);    // RAM size;

#define DEFAULT_COLOR 15        // WHITE
#define DEFAULT_BG_COLOR 4      // DARK BLUE
#define DEFAULT_MODE     0      // VDU display mode (0-7)

#define CURSOR_BLINK_INTERVAL_MS 600    // MS
