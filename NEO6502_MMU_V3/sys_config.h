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

#include <Arduino.h>

// --------------------------------------------------
// Limits
// --------------------------------------------------

#define MAX_LINE_LENGTH     128
#define MAX_NAME_LENGTH     16
#define MAX_FILE_LENGTH     32
#define MAX_CART            16
#define MAX_CONFIG          8
#define MAX_PER_CONFIG      8

// --------------------------------------------------
// Data Structures
// --------------------------------------------------

struct Cartridge {
  char name[MAX_NAME_LENGTH + 1];
  char file[MAX_FILE_LENGTH + 1];
  bool defined;
};

struct ConfigEntry {
  uint8_t cartIndex;
  uint8_t context;
};

struct Config {
  char name[MAX_NAME_LENGTH + 1];
  ConfigEntry entries[MAX_PER_CONFIG];
  uint8_t count;
  bool defined;
};

struct SystemConfig {
  uint8_t version;
  int8_t defaultConfig;
  char defaultName[MAX_NAME_LENGTH + 1];
};

// --------------------------------------------------
// Globals (owned by ini_parser.cpp)
// --------------------------------------------------

extern Cartridge    cartridges[MAX_CART];
extern Config       configs[MAX_CONFIG];
extern SystemConfig systemConfig;

// --------------------------------------------------
// Public API
// --------------------------------------------------

extern void loadFallbackProfile();

extern bool initializeSystemConfig();
