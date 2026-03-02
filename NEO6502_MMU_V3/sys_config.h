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
