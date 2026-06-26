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

#include <stdint.h>

#define MON_VERSION "v2.03.145"

// a little helper
constexpr char ctrl(char c) {
  return c & 0x1F;
}

void initMonitor();

void taskICMonitor();

/// <summary>
/// Feeds one byte into the console/terminal input path. Serial1 callers pass
/// allowReturnToICM=true. USB keyboard callers pass false so Ctrl-Z is
/// delivered as a normal control byte instead of returning to the Serial1
/// monitor.
/// </summary>
bool monitorConsoleInput(uint8_t c, bool allowReturnToICM);
