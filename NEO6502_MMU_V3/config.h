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

#define VERSION "3.5.283"

#define USE_VALIDATION 1

// Fixed RP2350 clock (adjust if you change it)
constexpr uint32_t CPU_CLOCK_HZ = 240000000u;

constexpr uint32_t DEFAULT_6502_CLOCK = (1 * 1000000L); // 1 MHZ;

constexpr uint32_t RAM_SIZE = (512 * 1024 * 1024);      // RAM size;
