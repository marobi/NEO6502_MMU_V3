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

#include "sys_config.h"
#include "memory_config.h"

// =====================================================
// NEO6502 STRICT INI CONFIGURATION SYSTEM
//
// Layout:
//   /system.ini            -> configuration file (root)
//   /system/<rom>.rom      -> cartridge images
//
// Syntax:
//   [system]
//   [cartridge.<name>]
//   [config.<name>]
//   [config.<name>.context.<n>]
//
// In config sections:
//   memory = <model>
//
// In config context sections:
//   load = bios
//   load = bios, ipc, micmon
//   load = bios
//   load = ipc, micmon
//
// Order is preserved exactly as written.
// =====================================================

#define SUPPORTED_VERSION   1

// =====================================================
// Error handling
// =====================================================

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
  INI_ERR_DEFAULT_NOT_FOUND,
  INI_ERR_INVALID_CONTEXT,
  INI_ERR_DUPLICATE_CONTEXT_SECTION,
  INI_ERR_MISSING_MEMORY_FIELD,
  INI_ERR_MEMORY_MODEL_MISMATCH
};

struct IniError {
  IniErrorCode code;
  int line;
};

static IniError lastIniError = { INI_OK, 0 };

static bool setIniError(IniErrorCode code, int line)
{
  lastIniError.code = code;
  lastIniError.line = line;
  return false;
}

static const char* iniErrorToString(IniErrorCode code)
{
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
  case INI_ERR_EMPTY_CONFIG: return "*E: Configuration has empty load list";
  case INI_ERR_NO_CONFIG_DEFINED: return "*E: No configuration defined";
  case INI_ERR_VERSION_MISMATCH: return "*E: Unsupported configuration version";
  case INI_ERR_DEFAULT_NOT_FOUND: return "*E: Default configuration not found";
  case INI_ERR_INVALID_CONTEXT: return "*E: Invalid context";
  case INI_ERR_DUPLICATE_CONTEXT_SECTION: return "*E: Duplicate config context section";
  case INI_ERR_MISSING_MEMORY_FIELD: return "*E: Missing config memory field";
  case INI_ERR_MEMORY_MODEL_MISMATCH: return "*E: Config memory model does not match active memory model";
  default: return "*E: Unknown INI error";
  }
}

// =====================================================
// Data storage
// =====================================================

Cartridge cartridges[MAX_CART];
Config configs[MAX_CONFIG];
SystemConfig systemConfig;

// =====================================================
// Fallback profile
// =====================================================

static const char* FALLBACK_CART_NAME = "boot";
static const char* FALLBACK_CART_FILE = "boot.rom";
static const char* FALLBACK_MEMORY_NAME = "default";

static void copyName(char* dst, const char* src)
{
  strncpy(dst, src, MAX_NAME_LENGTH);
  dst[MAX_NAME_LENGTH] = 0;
}

static void copyFile(char* dst, const char* src)
{
  strncpy(dst, src, MAX_FILE_LENGTH);
  dst[MAX_FILE_LENGTH] = 0;
}

static void resetTables()
{
  memset(cartridges, 0, sizeof(cartridges));
  memset(configs, 0, sizeof(configs));
  systemConfig.version = 0;
  systemConfig.defaultConfig = -1;
  memset(systemConfig.defaultName, 0, sizeof(systemConfig.defaultName));
}

void loadFallbackProfile()
{
  resetTables();

  copyName(cartridges[0].name, FALLBACK_CART_NAME);
  copyFile(cartridges[0].file, FALLBACK_CART_FILE);
  cartridges[0].defined = true;

  copyName(configs[0].name, "SAFE");
  copyName(configs[0].memory, FALLBACK_MEMORY_NAME);
  configs[0].defined = true;
  configs[0].memoryDefined = true;
  configs[0].count = 1;
  configs[0].contextDefined[DEFAULT_CONTEXT] = true;
  configs[0].entries[0].cartIndex = 0;
  configs[0].entries[0].context = DEFAULT_CONTEXT;

  systemConfig.version = SUPPORTED_VERSION;
  systemConfig.defaultConfig = 0;
}

// =====================================================
// Utilities
// =====================================================

static void trim(char* s)
{
  char* start = s;

  while (*start == ' ' || *start == '\t')
    start++;

  if (start != s)
    memmove(s, start, strlen(start) + 1);

  char* end = s + strlen(s);

  while (end > s &&
    (end[-1] == ' ' || end[-1] == '\t' ||
      end[-1] == '\r' || end[-1] == '\n'))
  {
    *--end = 0;
  }
}

static bool isValidIdentifier(const char* s)
{
  size_t len = strlen(s);

  if (len == 0 || len > MAX_NAME_LENGTH)
    return false;

  if (!isalpha((unsigned char)s[0]))
    return false;

  for (size_t i = 1; i < len; i++) {
    if (!isalnum((unsigned char)s[i]) && s[i] != '_')
      return false;
  }

  return true;
}

static int findCartridge(const char* name)
{
  int i;

  for (i = 0; i < MAX_CART; i++) {
    if (cartridges[i].defined && strcmp(cartridges[i].name, name) == 0)
      return i;
  }

  return -1;
}

static int findConfig(const char* name)
{
  int i;

  for (i = 0; i < MAX_CONFIG; i++) {
    if (configs[i].defined && strcmp(configs[i].name, name) == 0)
      return i;
  }

  return -1;
}

static bool parseContextStrict(const char* s, uint8_t& out)
{
  char* end;
  long v;

  if (!s || !*s)
    return false;

  v = strtol(s, &end, 10);

  if (*end != 0)
    return false;

  if (v < 0 || v >= NUM_CONTEXTS)
    return false;

  out = (uint8_t)v;
  return true;
}

// =====================================================
// Load list parser
// Accepts:
//   load = bios
//   load = bios, ipc, micmon
// =====================================================

static bool parseLoadList(char* value, Config& cfg, uint8_t ctx, int line)
{
  char* token;
  char* saveptr = 0;

  token = strtok_r(value, ",", &saveptr);

  while (token) {
    trim(token);

    if (token[0] == 0)
      return setIniError(INI_ERR_SYNTAX, line);

    if (!isValidIdentifier(token))
      return setIniError(INI_ERR_INVALID_IDENTIFIER, line);

    int cartIdx = findCartridge(token);
    if (cartIdx < 0)
      return setIniError(INI_ERR_UNKNOWN_REFERENCE, line);

    if (cfg.count >= MAX_PER_CONFIG)
      return setIniError(INI_ERR_LIMIT_EXCEEDED, line);

    cfg.entries[cfg.count].cartIndex = (uint8_t)cartIdx;
    cfg.entries[cfg.count].context = ctx;
    cfg.count++;

    token = strtok_r(0, ",", &saveptr);
  }

  return true;
}

// =====================================================
// Cross-check with memory config
// =====================================================

static bool validateSystemAgainstMemoryConfig()
{
  Config* cfg;

  if (systemConfig.defaultConfig < 0 || systemConfig.defaultConfig >= MAX_CONFIG)
    return setIniError(INI_ERR_DEFAULT_NOT_FOUND, 0);

  cfg = &configs[systemConfig.defaultConfig];

  if (!cfg->defined)
    return setIniError(INI_ERR_DEFAULT_NOT_FOUND, 0);

  if (strcmp(cfg->memory, memoryConfig.active_model) != 0)
    return setIniError(INI_ERR_MEMORY_MODEL_MISMATCH, 0);

  return true;
}

// =====================================================
// Main parser
// =====================================================

bool parseSystemIni(File& file)
{
  resetTables();
  lastIniError = { INI_OK, 0 };

  char line[MAX_LINE_LENGTH];
  int lineNumber = 0;

  enum {
    NONE,
    SYSTEM,
    CARTRIDGE,
    CONFIG,
    CONFIG_CONTEXT
  } state = NONE;

  int currentCart = -1;
  int currentConfig = -1;
  uint8_t currentContext = 0;

  while (file.available()) {
    size_t len = file.readBytesUntil('\n', line, MAX_LINE_LENGTH - 1);
    line[len] = 0;
    lineNumber++;

    if (len == MAX_LINE_LENGTH - 1)
      return setIniError(INI_ERR_LINE_TOO_LONG, lineNumber);

    char* comment = strchr(line, ';');
    if (comment)
      *comment = 0;

    trim(line);

    if (line[0] == 0 || line[0] == '#')
      continue;

    if (line[0] == '[') {
      char* end = strchr(line, ']');
      if (!end)
        return setIniError(INI_ERR_SYNTAX, lineNumber);

      *end = 0;

      char section[MAX_LINE_LENGTH];
      strncpy(section, line + 1, sizeof(section) - 1);
      section[sizeof(section) - 1] = 0;
      trim(section);

      currentCart = -1;
      currentConfig = -1;
      currentContext = 0;
      state = NONE;

      if (strcmp(section, "system") == 0) {
        state = SYSTEM;
        continue;
      }

      if (strncmp(section, "cartridge.", 10) == 0) {
        char* name = section + 10;
        trim(name);

        if (!isValidIdentifier(name))
          return setIniError(INI_ERR_INVALID_IDENTIFIER, lineNumber);

        if (findCartridge(name) >= 0)
          return setIniError(INI_ERR_DUPLICATE_SECTION, lineNumber);

        for (int i = 0; i < MAX_CART; i++) {
          if (!cartridges[i].defined) {
            copyName(cartridges[i].name, name);
            cartridges[i].defined = true;
            currentCart = i;
            state = CARTRIDGE;
            break;
          }
        }

        if (state != CARTRIDGE)
          return setIniError(INI_ERR_LIMIT_EXCEEDED, lineNumber);

        continue;
      }

      if (strncmp(section, "config.", 7) == 0) {
        char temp[MAX_LINE_LENGTH];
        char* tok;
        char* saveptr = 0;
        char* parts[4];
        int count = 0;

        strncpy(temp, section, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = 0;

        tok = strtok_r(temp, ".", &saveptr);
        while (tok && count < 4) {
          parts[count++] = tok;
          tok = strtok_r(0, ".", &saveptr);
        }

        if (count == 2) {
          if (strcmp(parts[0], "config") != 0)
            return setIniError(INI_ERR_SYNTAX, lineNumber);

          trim(parts[1]);

          if (!isValidIdentifier(parts[1]))
            return setIniError(INI_ERR_INVALID_IDENTIFIER, lineNumber);

          currentConfig = findConfig(parts[1]);
          if (currentConfig >= 0)
            return setIniError(INI_ERR_DUPLICATE_SECTION, lineNumber);

          for (int i = 0; i < MAX_CONFIG; i++) {
            if (!configs[i].defined) {
              copyName(configs[i].name, parts[1]);
              configs[i].defined = true;
              configs[i].memoryDefined = false;
              configs[i].count = 0;
              memset(configs[i].contextDefined, 0, sizeof(configs[i].contextDefined));
              currentConfig = i;
              break;
            }
          }

          if (currentConfig < 0)
            return setIniError(INI_ERR_LIMIT_EXCEEDED, lineNumber);

          state = CONFIG;
          continue;
        }

        if (count == 4) {
          uint8_t ctx;

          if (strcmp(parts[0], "config") != 0 ||
            strcmp(parts[2], "context") != 0)
            return setIniError(INI_ERR_SYNTAX, lineNumber);

          trim(parts[1]);
          trim(parts[3]);

          if (!isValidIdentifier(parts[1]))
            return setIniError(INI_ERR_INVALID_IDENTIFIER, lineNumber);

          if (!parseContextStrict(parts[3], ctx))
            return setIniError(INI_ERR_INVALID_CONTEXT, lineNumber);

          currentConfig = findConfig(parts[1]);
          if (currentConfig < 0)
            return setIniError(INI_ERR_UNKNOWN_REFERENCE, lineNumber);

          if (configs[currentConfig].contextDefined[ctx])
            return setIniError(INI_ERR_DUPLICATE_CONTEXT_SECTION, lineNumber);

          configs[currentConfig].contextDefined[ctx] = true;
          currentContext = ctx;
          state = CONFIG_CONTEXT;
          continue;
        }

        return setIniError(INI_ERR_SYNTAX, lineNumber);
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
      if (strcmp(key, "version") == 0) {
        systemConfig.version = atoi(value);
      }
      else if (strcmp(key, "default") == 0) {
        if (!isValidIdentifier(value))
          return setIniError(INI_ERR_INVALID_IDENTIFIER, lineNumber);

        copyName(systemConfig.defaultName, value);
      }
      else {
        return setIniError(INI_ERR_SYNTAX, lineNumber);
      }
    }
    else if (state == CARTRIDGE) {
      if (strcmp(key, "file") == 0) {
        if (strlen(value) > MAX_FILE_LENGTH)
          return setIniError(INI_ERR_LIMIT_EXCEEDED, lineNumber);

        copyFile(cartridges[currentCart].file, value);
      }
      else {
        return setIniError(INI_ERR_SYNTAX, lineNumber);
      }
    }
    else if (state == CONFIG) {
      if (strcmp(key, "memory") == 0) {
        if (!isValidIdentifier(value))
          return setIniError(INI_ERR_INVALID_IDENTIFIER, lineNumber);

        copyName(configs[currentConfig].memory, value);
        configs[currentConfig].memoryDefined = true;
      }
      else {
        return setIniError(INI_ERR_SYNTAX, lineNumber);
      }
    }
    else if (state == CONFIG_CONTEXT) {
      if (strcmp(key, "load") == 0) {
        if (!parseLoadList(value, configs[currentConfig], currentContext, lineNumber))
          return false;
      }
      else {
        return setIniError(INI_ERR_SYNTAX, lineNumber);
      }
    }
    else {
      return setIniError(INI_ERR_SYNTAX, lineNumber);
    }
  }

  // =====================================================
  // Post validation
  // =====================================================

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

      if (!configs[i].memoryDefined)
        return setIniError(INI_ERR_MISSING_MEMORY_FIELD, 0);

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
    for (int i = 0; i < MAX_CONFIG; i++) {
      if (configs[i].defined) {
        systemConfig.defaultConfig = i;
        break;
      }
    }
  }

  if (systemConfig.defaultConfig < 0)
    return setIniError(INI_ERR_NO_CONFIG_DEFINED, 0);

  if (!validateSystemAgainstMemoryConfig())
    return false;

  return true;
}

// =====================================================
// Initialization
// =====================================================

bool initializeSystemConfig()
{
  resetTables();

  File file = LittleFS.open("/system.ini", "r");

  if (!file) {
    Serial1.println("*E: INI file missing. Using fallback profile.");
    loadFallbackProfile();
    return false;
  }

  if (!parseSystemIni(file)) {
    Serial1.printf("*E: INI error: %s (code=%d, line=%d)\n",
      iniErrorToString(lastIniError.code),
      lastIniError.code,
      lastIniError.line);

    if (lastIniError.code == INI_ERR_MEMORY_MODEL_MISMATCH &&
      systemConfig.defaultConfig >= 0 &&
      configs[systemConfig.defaultConfig].defined) {
      Serial1.printf("*E: Config '%s' requires memory model '%s', active model is '%s'\n",
        configs[systemConfig.defaultConfig].name,
        configs[systemConfig.defaultConfig].memory,
        memoryConfig.active_model);
    }

    file.close();

    Serial1.println("*I: Using fallback profile.");
    loadFallbackProfile();
    return false;
  }

  file.close();
  Serial1.println("\n*I: System config loaded");
  return true;
}

// =====================================================
// Dump
// =====================================================

void dumpSystemConfig()
{
  int i;
  int j;

  Serial1.println("--------------------------------------------------");
  Serial1.println("SYSTEM CONFIG");
  Serial1.println("--------------------------------------------------");

  Serial1.printf("Version        : %d\n", systemConfig.version);
  Serial1.printf("Default config : %s\n",
    systemConfig.defaultName[0] ? systemConfig.defaultName : "<first>");
  Serial1.printf("Resolved index : %d\n", systemConfig.defaultConfig);
  Serial1.println();

  Serial1.println("CARTRIDGES");
  Serial1.println("--------------------------------------------------");

  for (i = 0; i < MAX_CART; i++) {
    if (!cartridges[i].defined)
      continue;

    Serial1.printf("[%02d] %-16s -> %s\n",
      i,
      cartridges[i].name,
      cartridges[i].file);
  }

  Serial1.println();
  Serial1.println("CONFIGS");
  Serial1.println("--------------------------------------------------");

  for (i = 0; i < MAX_CONFIG; i++) {
    Config* cfg;
    bool any = false;

    if (!configs[i].defined)
      continue;

    cfg = &configs[i];

    Serial1.printf("Config [%02d] : %s\n", i, cfg->name);
    Serial1.printf("  Memory      : %s\n", cfg->memory);

    for (j = 0; j < NUM_CONTEXTS; j++) {
      int k;
      bool first = true;

      for (k = 0; k < cfg->count; k++) {
        if (cfg->entries[k].context != j)
          continue;

        if (first) {
          Serial1.printf("  Context %d   : ", j);
          first = false;
          any = true;
        }
        else {
          Serial1.print(", ");
        }

        Serial1.print(cartridges[cfg->entries[k].cartIndex].name);
      }

      if (!first)
        Serial1.println();
    }

    if (!any)
      Serial1.println("  <empty>");

    Serial1.println();
  }

  Serial1.println("--------------------------------------------------");
}
