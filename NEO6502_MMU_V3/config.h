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

#define VERSION "3.9.431"

#define USE_VALIDATION 0

// -------------------------------------------------------------------------------------

// Fixed RP2350 clock (adjust if you change it)
constexpr uint32_t SYS_CLOCK_HZ = 240 * MHZ;

constexpr uint32_t DEFAULT_6502_CLOCK = (4 * MHZ);   // 4 MHZ;

constexpr uint32_t RAM_SIZE = (512ul * 1024ul);      // RAM size;
