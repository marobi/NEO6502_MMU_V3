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
// sys_config.h
// sys_config.h
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

#include <LittleFS.h>

#include "mmu.h"

#define MAX_NAME_LENGTH   16
#define MAX_FILE_LENGTH   32
#define MAX_LINE_LENGTH   160

#define MAX_CART          16
#define MAX_CONFIG        16
#define MAX_PER_CONFIG    64

struct ConfigEntry {
  uint8_t cartIndex;
  uint8_t context;
};

struct Cartridge {
  char name[MAX_NAME_LENGTH + 1];
  char file[MAX_FILE_LENGTH + 1];
  bool defined;
};

struct Config {
  char name[MAX_NAME_LENGTH + 1];
  char memory[MAX_NAME_LENGTH + 1];
  bool defined;
  bool memoryDefined;
  uint8_t count;
  bool contextDefined[NUM_CONTEXTS];
  ConfigEntry entries[MAX_PER_CONFIG];
};

struct SystemConfig {
  uint8_t version;
  int defaultConfig;
  char defaultName[MAX_NAME_LENGTH + 1];
};

extern Cartridge cartridges[MAX_CART];
extern Config configs[MAX_CONFIG];
extern SystemConfig systemConfig;

bool parseSystemIni(File& file);
bool initializeSystemConfig();
void loadFallbackProfile();
void dumpSystemConfig();
