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
#include <LittleFS.h>

#include "sys_config.h"
#include "mmu.h"

// =====================================================
// NEO6502 STRICT INI CONFIGURATION SYSTEM
// Layout:
//   /system.ini            -> configuration file (root)
//   /system/<rom>.rom      -> cartridge images
// =====================================================

#define SUPPORTED_VERSION   1

/// <summary>
/// Error codes for INI parsing and validation. Used for error reporting and debugging.
/// </summary>
enum IniErrorCode {
  INI_OK = 0,
  INI_ERR_LINE_TOO_LONG,
  INI_ERR_SYNTAX,
  INI_ERR_INVALID_IDENTIFIER,
  INI_ERR_DUPLICATE_SECTION,
  INI_ERR_LIMIT_EXCEEDED,
  INI_ERR_UNKNOWN_REFERENCE,
  INI_ERR_MISSING_FILE_FIELD,
  INI_ERR_FILE_NOT_FOUND,
  INI_ERR_EMPTY_CONFIG,
  INI_ERR_NO_CONFIG_DEFINED,
  INI_ERR_VERSION_MISMATCH,
  INI_ERR_DEFAULT_NOT_FOUND
};

struct IniError {
  IniErrorCode code;
  int line;
};

static IniError lastIniError = { INI_OK, 0 };

static bool setIniError(IniErrorCode code, int line) {
  lastIniError.code = code;
  lastIniError.line = line;
  return false;
}

/// <summary>
/// Error messages corresponding to IniErrorCode values for debugging purposes.
/// </summary>
/// <param name="code"></param>
/// <returns></returns>
static const char* iniErrorToString(IniErrorCode code) {
  switch (code) {
  case INI_OK: return "*D: No error";
  case INI_ERR_LINE_TOO_LONG: return "*E: Line too long";
  case INI_ERR_SYNTAX: return "*E: Syntax error";
  case INI_ERR_INVALID_IDENTIFIER: return "*E: Invalid identifier";
  case INI_ERR_DUPLICATE_SECTION: return "*E: Duplicate section";
  case INI_ERR_LIMIT_EXCEEDED: return "*E: Resource limit exceeded";
  case INI_ERR_UNKNOWN_REFERENCE: return "*E: Unknown reference";
  case INI_ERR_MISSING_FILE_FIELD: return "*E: Missing cartridge file field";
  case INI_ERR_FILE_NOT_FOUND: return "*E: Referenced cartridge file not found";
  case INI_ERR_EMPTY_CONFIG: return "*E: Configuration has empty order list";
  case INI_ERR_NO_CONFIG_DEFINED: return "*E: No configuration defined";
  case INI_ERR_VERSION_MISMATCH: return "*E: Unsupported configuration version";
  case INI_ERR_DEFAULT_NOT_FOUND: return "*E: Default configuration not found";
  default: return "*E: Unknown INI error";
  }
}

// =====================================================
// Data Structures
// =====================================================
Cartridge cartridges[MAX_CART];
Config configs[MAX_CONFIG];
SystemConfig systemConfig;

// =====================================================
// FALLBACK PROFILE (ROMs still loaded from /system/)
// =====================================================
static const char* FALLBACK_CART_NAME = "boot";
static const char* FALLBACK_CART_FILE = "boot.rom";

/// <summary>
/// reset all tables and system config to default empty state.
/// </summary>
static void resetTables() {
  memset(cartridges, 0, sizeof(cartridges));
  memset(configs, 0, sizeof(configs));
  systemConfig.version = 0;
  systemConfig.defaultConfig = -1;
  memset(systemConfig.defaultName, 0, sizeof(systemConfig.defaultName));
}

/// <summary>
/// load a fallback profile with a single cartridge and configuration.
/// </summary>
void loadFallbackProfile() {

  resetTables();

  strncpy(cartridges[0].name, FALLBACK_CART_NAME, MAX_NAME_LENGTH);
  strncpy(cartridges[0].file, FALLBACK_CART_FILE, MAX_FILE_LENGTH);
  cartridges[0].defined = true;

  strncpy(configs[0].name, "SAFE", MAX_NAME_LENGTH);
  configs[0].defined = true;
  configs[0].count = 1;
  configs[0].entries[0].cartIndex = 0;
  configs[0].entries[0].context = DEFAULT_CONTEXT;

  systemConfig.version = SUPPORTED_VERSION;
  systemConfig.defaultConfig = 0;
}

// =====================================================
// UTILITIES
// =====================================================

static void trim(char* s) {
  char* start = s;
  while (*start == ' ' || *start == '\t') start++;
  if (start != s) memmove(s, start, strlen(start) + 1);

  char* end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    *--end = 0;
}

static bool isValidIdentifier(const char* s) {
  size_t len = strlen(s);
  if (len == 0 || len > MAX_NAME_LENGTH) return false;
  if (!isalpha((unsigned char)s[0])) return false;
  for (size_t i = 1; i < len; i++)
    if (!isalnum((unsigned char)s[i]) && s[i] != '_')
      return false;
  return true;
}

static int findCartridge(const char* name) {
  for (int i = 0; i < MAX_CART; i++)
    if (cartridges[i].defined && strcmp(cartridges[i].name, name) == 0)
      return i;
  return -1;
}

static int findConfig(const char* name) {
  for (int i = 0; i < MAX_CONFIG; i++)
    if (configs[i].defined && strcmp(configs[i].name, name) == 0)
      return i;
  return -1;
}

/// <summary>
/// parse a map list in the format "cart1:ctx1, cart2:ctx2, ..." and populate the given Config structure.
/// </summary>
/// <param name="value"></param>
/// <param name="cfg"></param>
/// <param name="line"></param>
/// <returns></returns>
static bool parseMapList(char* value, Config& cfg, int line) {

  char* p = value;

  while (*p) {

    while (*p == ' ' || *p == '\t')
      p++;

    char name[MAX_NAME_LENGTH + 1];
    char ctxStr[8];

    size_t nameLen = 0;
    size_t ctxLen = 0;

    // Parse cartridge name
    while (*p && *p != ':' && *p != ',') {
      if (nameLen >= MAX_NAME_LENGTH)
        return setIniError(INI_ERR_LIMIT_EXCEEDED, line);
      name[nameLen++] = *p++;
    }

    name[nameLen] = 0;

    if (*p != ':')
      return setIniError(INI_ERR_SYNTAX, line);

    p++; // skip ':'

    // Parse context number
    while (*p && *p != ',') {
      if (ctxLen >= sizeof(ctxStr) - 1)
        return setIniError(INI_ERR_LIMIT_EXCEEDED, line);
      ctxStr[ctxLen++] = *p++;
    }

    ctxStr[ctxLen] = 0;

    trim(name);
    trim(ctxStr);

    if (!isValidIdentifier(name))
      return setIniError(INI_ERR_INVALID_IDENTIFIER, line);

    int cartIdx = findCartridge(name);
    if (cartIdx < 0)
      return setIniError(INI_ERR_UNKNOWN_REFERENCE, line);

    int ctx = atoi(ctxStr);

    if (ctx < 0 || ctx >= NUM_CONTEXTS)
      return setIniError(INI_ERR_LIMIT_EXCEEDED, line);

    // Prevent duplicate context inside same config
    //for (uint8_t i = 0; i < cfg.count; i++) {
    //  if (cfg.entries[i].context == ctx)
    //    return setIniError(INI_ERR_DUPLICATE_SECTION, line);
    //}

    if (cfg.count >= MAX_PER_CONFIG)
      return setIniError(INI_ERR_LIMIT_EXCEEDED, line);

    cfg.entries[cfg.count].cartIndex = cartIdx;
    cfg.entries[cfg.count].context = ctx;
    cfg.count++;

    if (*p == ',')
      p++;
  }

  if (cfg.count == 0)
    return setIniError(INI_ERR_EMPTY_CONFIG, line);

  return true;
}

/// <summary>
/// the parser for the system.ini file. 
/// It reads the file line by line, maintains state for the current section, 
/// and populates the cartridges, configs, and systemConfig structures. 
/// It also performs validation and error reporting with detailed error codes and line numbers.
/// </summary>
/// <param name="file"></param>
/// <returns></returns>
bool parseSystemIni(File& file) {

  resetTables();
  lastIniError = { INI_OK, 0 };

  char line[MAX_LINE_LENGTH];
  int lineNumber = 0;

  enum { NONE, SYSTEM, CARTRIDGE, CONFIG } state = NONE;
  int currentCart = -1;
  int currentConfig = -1;

  while (file.available()) {

    size_t len = file.readBytesUntil('\n', line, MAX_LINE_LENGTH - 1);
    line[len] = 0;
    lineNumber++;

    if (len == MAX_LINE_LENGTH - 1)
      return setIniError(INI_ERR_LINE_TOO_LONG, lineNumber);

    char* comment = strchr(line, ';');
    if (comment) *comment = 0;
    // ------------------

    trim(line);

    if (line[0] == 0 || line[0] == '#' || line[0] == ';')
      continue;

    if (line[0] == '[') {
      char* end = strchr(line, ']');
      if (!end)
        return setIniError(INI_ERR_SYNTAX, lineNumber);

      *end = 0;
      char* section = line + 1;
      trim(section);

      if (strcmp(section, "system") == 0) {
        state = SYSTEM;
        continue;
      }

      if (strncmp(section, "cartridge ", 10) == 0) {
        char* name = section + 10;
        trim(name);
        if (!isValidIdentifier(name))
          return setIniError(INI_ERR_INVALID_IDENTIFIER, lineNumber);
        if (findCartridge(name) >= 0)
          return setIniError(INI_ERR_DUPLICATE_SECTION, lineNumber);

        for (int i = 0; i < MAX_CART; i++) {
          if (!cartridges[i].defined) {
            strncpy(cartridges[i].name, name, MAX_NAME_LENGTH);
            cartridges[i].defined = true;
            currentCart = i;
            state = CARTRIDGE;
            break;
          }
        }
        continue;
      }

      if (strncmp(section, "config ", 7) == 0) {
        char* name = section + 7;
        trim(name);
        if (!isValidIdentifier(name))
          return setIniError(INI_ERR_INVALID_IDENTIFIER, lineNumber);
        if (findConfig(name) >= 0)
          return setIniError(INI_ERR_DUPLICATE_SECTION, lineNumber);

        for (int i = 0; i < MAX_CONFIG; i++) {
          if (!configs[i].defined) {
            strncpy(configs[i].name, name, MAX_NAME_LENGTH);
            configs[i].defined = true;
            configs[i].count = 0;
            currentConfig = i;
            state = CONFIG;
            break;
          }
        }
        continue;
      }

      return setIniError(INI_ERR_SYNTAX, lineNumber);
    }

    char* eq = strchr(line, '=');
    if (!eq)
      return setIniError(INI_ERR_SYNTAX, lineNumber);

    *eq = 0;
    char* key = line;
    char* value = eq + 1;
    trim(key);
    trim(value);

    if (state == SYSTEM) {
      if (strcmp(key, "version") == 0)
        systemConfig.version = atoi(value);
      else if (strcmp(key, "default") == 0) {
        if (!isValidIdentifier(value))
          return setIniError(INI_ERR_INVALID_IDENTIFIER, lineNumber);
        strncpy(systemConfig.defaultName, value, MAX_NAME_LENGTH);
      }
      else
        return setIniError(INI_ERR_SYNTAX, lineNumber);
    }
    else if (state == CARTRIDGE) {
      if (strcmp(key, "file") == 0) {
        if (strlen(value) > MAX_FILE_LENGTH)
          return setIniError(INI_ERR_LIMIT_EXCEEDED, lineNumber);
        strncpy(cartridges[currentCart].file, value, MAX_FILE_LENGTH);
      }
      else
        return setIniError(INI_ERR_SYNTAX, lineNumber);
    }
    else if (state == CONFIG) {
      if (strcmp(key, "map") == 0) {
        if (!parseMapList(value, configs[currentConfig], lineNumber))
          return false;
      }
      else
        return setIniError(INI_ERR_SYNTAX, lineNumber);
    }
    else
      return setIniError(INI_ERR_SYNTAX, lineNumber);
  }

  // ---------------- POST VALIDATION ----------------

  if (systemConfig.version != SUPPORTED_VERSION)
    return setIniError(INI_ERR_VERSION_MISMATCH, lineNumber);

  bool hasConfig = false;

  for (int i = 0; i < MAX_CART; i++) {
    if (cartridges[i].defined) {
      if (cartridges[i].file[0] == 0)
        return setIniError(INI_ERR_MISSING_FILE_FIELD, 0);

      char fullPath[64];
      snprintf(fullPath, sizeof(fullPath), "/system/%s", cartridges[i].file);
      if (!LittleFS.exists(fullPath))
        return setIniError(INI_ERR_FILE_NOT_FOUND, 0);
    }
  }

  for (int i = 0; i < MAX_CONFIG; i++) {
    if (configs[i].defined) {
      hasConfig = true;
      if (configs[i].count == 0)
        return setIniError(INI_ERR_EMPTY_CONFIG, 0);
    }
  }

  if (!hasConfig)
    return setIniError(INI_ERR_NO_CONFIG_DEFINED, 0);

  if (systemConfig.defaultName[0] != 0) {
    int resolved = findConfig(systemConfig.defaultName);
    if (resolved < 0)
      return setIniError(INI_ERR_DEFAULT_NOT_FOUND, 0);
    systemConfig.defaultConfig = resolved;
  }
  else {
    for (int i = 0; i < MAX_CONFIG; i++)
      if (configs[i].defined) {
        systemConfig.defaultConfig = i;
        break;
      }
  }

  if (systemConfig.defaultConfig < 0)
    return setIniError(INI_ERR_NO_CONFIG_DEFINED, 0);

  return true;
}

/// <summary>
/// initialise the system configuration by reading and parsing the /system.ini file from LittleFS.
/// </summary>
/// <returns></returns>
bool initializeSystemConfig() {

  resetTables();

  File file = LittleFS.open("/system.ini", "r");

  if (!file) {
    Serial.println("*E: INI file missing. Using fallback profile.");
    loadFallbackProfile();
    return false;
  }

  if (!parseSystemIni(file)) {

    Serial.printf("*E: INI error: %s (code=%d, line=%d)\n",
      iniErrorToString(lastIniError.code),
      lastIniError.code,
      lastIniError.line);

    file.close();

    Serial.println("*I: Using fallback profile.");
    loadFallbackProfile();
    return false;
  }
  Serial.println("\n*I: System config loaded");

  file.close();
  return true;
}
