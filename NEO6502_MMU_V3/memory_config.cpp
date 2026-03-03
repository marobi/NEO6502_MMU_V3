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
#include "memory_config.h"
#include "mmu.h"

#define MEMORY_SUPPORTED_VERSION  1

// =====================================================
// ERROR HANDLING
// =====================================================

struct MemoryIniError {
  MemoryIniErrorCode code;
  int line;
};

static MemoryIniError lastMemoryIniError = { MEMORY_INI_OK, 0 };

static bool setMemoryIniError(MemoryIniErrorCode code, int line)
{
  lastMemoryIniError.code = code;
  lastMemoryIniError.line = line;
  return false;
}

const char* memoryIniErrorToString(MemoryIniErrorCode code)
{
  switch (code) {
  case MEMORY_INI_OK: return "*D: No error";
  case MEMORY_INI_ERR_LINE_TOO_LONG: return "*E: Line too long";
  case MEMORY_INI_ERR_SYNTAX: return "*E: Syntax error";
  case MEMORY_INI_ERR_INVALID_IDENTIFIER: return "*E: Invalid identifier";
  case MEMORY_INI_ERR_DUPLICATE_SECTION: return "*E: Duplicate section";
  case MEMORY_INI_ERR_LIMIT_EXCEEDED: return "*E: Resource limit exceeded";
  case MEMORY_INI_ERR_UNKNOWN_MODEL: return "*E: Unknown model";
  case MEMORY_INI_ERR_VERSION_MISMATCH: return "*E: Unsupported configuration version";
  case MEMORY_INI_ERR_VALIDATION_FAILED: return "*E: Memory layout validation failed";
  default: return "*E: Unknown MEMORY.INI error";
  }
}

// =====================================================
// GLOBAL CONFIG
// =====================================================

MemoryConfig memoryConfig;

// =====================================================
// FALLBACK PROFILE
// =====================================================

static void resetTables()
{
  memset(&memoryConfig, 0, sizeof(memoryConfig));
}

static void loadFallbackProfile()
{
  resetTables();

  memoryConfig.version = MEMORY_SUPPORTED_VERSION;
  strncpy(memoryConfig.active_model, "SAFE", MAX_MEMORY_NAME);

  strncpy(memoryConfig.models[0].name, "SAFE", MAX_MEMORY_NAME);
  memoryConfig.models[0].contexts = 1;
  memoryConfig.models[0].region_count = 2;

  // Pages 0–14 shared normal
  strncpy(memoryConfig.models[0].regions[0].name, "ram", MAX_MEMORY_NAME);
  memoryConfig.models[0].regions[0].start_position = 0;
  memoryConfig.models[0].regions[0].pages = 15;
  memoryConfig.models[0].regions[0].shared = true;
  memoryConfig.models[0].regions[0].type = MEMORY_REGION_NORMAL;
  memoryConfig.models[0].regions[0].trap_write = false;

  // Page 15 shared IO with trap
  strncpy(memoryConfig.models[0].regions[1].name, "io", MAX_MEMORY_NAME);
  memoryConfig.models[0].regions[1].start_position = 15;
  memoryConfig.models[0].regions[1].pages = 1;
  memoryConfig.models[0].regions[1].shared = true;
  memoryConfig.models[0].regions[1].type = MEMORY_REGION_IO;
  memoryConfig.models[0].regions[1].trap_write = true;

  memoryConfig.model_count = 1;
}

// =====================================================
// UTILITIES
// =====================================================

static void trim(char* s)
{
  char* start = s;
  while (*start == ' ' || *start == '\t') start++;
  if (start != s) memmove(s, start, strlen(start) + 1);

  char* end = s + strlen(s);
  while (end > s &&
    (end[-1] == ' ' || end[-1] == '\t' ||
      end[-1] == '\r' || end[-1] == '\n'))
    *--end = 0;
}

static void stripInlineComment(char* s)
{
  char* p = strchr(s, ';');
  if (p) *p = 0;
}

static bool isValidIdentifier(const char* s)
{
  size_t len = strlen(s);
  if (len == 0 || len > MAX_MEMORY_NAME) return false;
  if (!isalpha((unsigned char)s[0])) return false;

  for (size_t i = 1; i < len; i++)
    if (!isalnum((unsigned char)s[i]) && s[i] != '_')
      return false;

  return true;
}

static int findModel(const char* name)
{
  for (int i = 0; i < MAX_MEMORY_MODELS; i++)
    if (memoryConfig.models[i].name[0] &&
      strcmp(memoryConfig.models[i].name, name) == 0)
      return i;

  return -1;
}

// =====================================================
// PARSER
// =====================================================

bool parseMemoryIni(File& file)
{
  resetTables();
  lastMemoryIniError = { MEMORY_INI_OK, 0 };

  char line[MAX_MEMORY_LINE];
  int lineNumber = 0;

  int currentModel = -1;
  int currentRegion = -1;

  while (file.available())
  {
    size_t len = file.readBytesUntil('\n', line, MAX_MEMORY_LINE - 1);
    line[len] = 0;
    lineNumber++;

    if (len == MAX_MEMORY_LINE - 1)
      return setMemoryIniError(MEMORY_INI_ERR_LINE_TOO_LONG, lineNumber);

    stripInlineComment(line);
    trim(line);

    if (line[0] == 0 || line[0] == '#' || line[0] == ';')
      continue;

    if (line[0] == '[')
    {
      char* end = strchr(line, ']');
      if (!end)
        return setMemoryIniError(MEMORY_INI_ERR_SYNTAX, lineNumber);

      *end = 0;
      char* section = line + 1;
      trim(section);

      currentRegion = -1;

      if (strcmp(section, "system") == 0) {
        currentModel = -1;
        continue;
      }

      if (strncmp(section, "model.", 6) == 0)
      {
        char* name = section + 6;
        char* regionPtr = strstr(name, ".region.");

        if (!regionPtr)
        {
          if (!isValidIdentifier(name))
            return setMemoryIniError(MEMORY_INI_ERR_INVALID_IDENTIFIER, lineNumber);

          if (findModel(name) >= 0)
            return setMemoryIniError(MEMORY_INI_ERR_DUPLICATE_SECTION, lineNumber);

          for (int i = 0; i < MAX_MEMORY_MODELS; i++)
          {
            if (!memoryConfig.models[i].name[0])
            {
              strncpy(memoryConfig.models[i].name, name, MAX_MEMORY_NAME);
              currentModel = i;
              memoryConfig.model_count++;
              break;
            }
          }
          continue;
        }

        *regionPtr = 0;
        char* modelName = name;
        char* regionName = regionPtr + 8;

        int m = findModel(modelName);
        if (m < 0)
          return setMemoryIniError(MEMORY_INI_ERR_UNKNOWN_MODEL, lineNumber);

        if (!isValidIdentifier(regionName))
          return setMemoryIniError(MEMORY_INI_ERR_INVALID_IDENTIFIER, lineNumber);

        if (memoryConfig.models[m].region_count >= MAX_MEMORY_REGIONS)
          return setMemoryIniError(MEMORY_INI_ERR_LIMIT_EXCEEDED, lineNumber);

        currentModel = m;
        currentRegion = memoryConfig.models[m].region_count++;

        strncpy(memoryConfig.models[m].regions[currentRegion].name,
          regionName, MAX_MEMORY_NAME);

        continue;
      }

      return setMemoryIniError(MEMORY_INI_ERR_SYNTAX, lineNumber);
    }

    char* eq = strchr(line, '=');
    if (!eq)
      return setMemoryIniError(MEMORY_INI_ERR_SYNTAX, lineNumber);

    *eq = 0;
    char* key = line;
    char* value = eq + 1;
    trim(key);
    trim(value);

    if (strcmp(key, "config_version") == 0)
      memoryConfig.version = atoi(value);

    else if (strcmp(key, "active_model") == 0)
      strncpy(memoryConfig.active_model, value, MAX_MEMORY_NAME);

    else if (currentModel >= 0 && currentRegion < 0)
    {
      if (strcmp(key, "contexts") == 0)
        memoryConfig.models[currentModel].contexts = atoi(value);
      else
        return setMemoryIniError(MEMORY_INI_ERR_SYNTAX, lineNumber);
    }

    else if (currentModel >= 0 && currentRegion >= 0)
    {
      MemoryRegion* r = &memoryConfig.models[currentModel].regions[currentRegion];

      if (strcmp(key, "start_position") == 0)
        r->start_position = atoi(value);
      else if (strcmp(key, "pages") == 0)
        r->pages = atoi(value);
      else if (strcmp(key, "type") == 0)
        r->type = (strcmp(value, "io") == 0) ? MEMORY_REGION_IO : MEMORY_REGION_NORMAL;
      else if (strcmp(key, "shared") == 0)
        r->shared = (strcmp(value, "true") == 0);
      else if (strcmp(key, "trap_write") == 0)
        r->trap_write = (strcmp(value, "true") == 0);
      else
        return setMemoryIniError(MEMORY_INI_ERR_SYNTAX, lineNumber);
    }
    else
      return setMemoryIniError(MEMORY_INI_ERR_SYNTAX, lineNumber);
  }

  if (memoryConfig.version != MEMORY_SUPPORTED_VERSION)
    return setMemoryIniError(MEMORY_INI_ERR_VERSION_MISMATCH, 0);

  if (findModel(memoryConfig.active_model) < 0)
    return setMemoryIniError(MEMORY_INI_ERR_UNKNOWN_MODEL, 0);

  return true;
}

// =====================================================
// INITIALIZER
// =====================================================

bool initializeMemoryConfig()
{
  resetTables();

  File file = LittleFS.open("/memory.ini", "r");

  if (!file) {
    Serial.println("*E: MEMORY.INI missing. Using fallback profile.");
    loadFallbackProfile();
    return false;
  }

  if (!parseMemoryIni(file)) {

    Serial.printf("*E: MEMORY.INI error: %s (code=%d, line=%d)\n",
      memoryIniErrorToString(lastMemoryIniError.code),
      lastMemoryIniError.code,
      lastMemoryIniError.line);

    file.close();

    Serial.println("*E: Using fallback profile.");
    loadFallbackProfile();
    return false;
  }

  file.close();

  Serial.println("\n*I: Memory config loaded.");
  return true;
}

// =====================================================
// MMU CONFIGURATION (PRIVATE FIRST, SHARED LAST)
// =====================================================

bool configureMMUFromActiveModel()
{
  int active = findModel(memoryConfig.active_model);
  if (active < 0)
    return false;

  MemoryModel* model = &memoryConfig.models[active];

  if (model->contexts == 0 ||
    model->contexts > NUM_CONTEXTS)
    return false;

  uint8_t nextPhysical = 0;

  // -------------------------------------------------
  // 1. Allocate PRIVATE regions first (per context)
  // -------------------------------------------------

  for (int ctx = 0; ctx < model->contexts; ctx++)
  {
    for (int r = 0; r < model->region_count; r++)
    {
      MemoryRegion* region = &model->regions[r];

      if (region->shared)
        continue;

      for (int p = 0; p < region->pages; p++)
      {
        uint8_t logical = region->start_position + p;

        if (logical >= NUM_CONTEXT_PAGES)
          return false;

        if (nextPhysical >= 128)
          return false;

        uint8_t phys = nextPhysical++;

        if (region->type == MEMORY_REGION_IO ||
          region->trap_write)
        {
          phys |= 0x80;
        }

        if (!writeMMUPage(ctx, logical, phys))
          return false;
      }
    }
  }

  // -------------------------------------------------
  // 2. Allocate SHARED regions once (after private)
  // -------------------------------------------------

  for (int r = 0; r < model->region_count; r++)
  {
    MemoryRegion* region = &model->regions[r];

    if (!region->shared)
      continue;

    for (int p = 0; p < region->pages; p++)
    {
      uint8_t logical = region->start_position + p;

      if (logical >= NUM_CONTEXT_PAGES)
        return false;

      if (nextPhysical >= 128)
        return false;

      uint8_t phys = nextPhysical++;

      if (region->type == MEMORY_REGION_IO ||
        region->trap_write)
      {
        phys |= 0x80;
      }

      for (int ctx = 0; ctx < model->contexts; ctx++)
      {
        if (!writeMMUPage(ctx, logical, phys))
          return false;
      }
    }
  }

  return true;
}


// =====================================================
// MODEL DUMP
// =====================================================

void dumpMemoryConfig()
{
  Serial.println("--------------------------------------------------");
  Serial.println("ACTIVE MEMORY MODEL");
  Serial.println("--------------------------------------------------");

  Serial.print("Version      : ");
  Serial.println(memoryConfig.version);

  Serial.print("Active model : ");
  Serial.println(memoryConfig.active_model);

  int active = -1;

  for (int i = 0; i < MAX_MEMORY_MODELS; i++) {
    if (memoryConfig.models[i].name[0] &&
      strcmp(memoryConfig.models[i].name,
        memoryConfig.active_model) == 0) {
      active = i;
      break;
    }
  }

  if (active < 0) {
    Serial.println("*E: Active model not found");
    Serial.println("--------------------------------------------------");
    return;
  }

  MemoryModel* model = &memoryConfig.models[active];

  Serial.print("Contexts     : ");
  Serial.println(model->contexts);

  Serial.print("Region count : ");
  Serial.println(model->region_count);

  Serial.println();

  for (int r = 0; r < model->region_count; r++)
  {
    MemoryRegion* region = &model->regions[r];

    Serial.print("Region: ");
    Serial.println(region->name);

    Serial.print("  Start page : ");
    Serial.println(region->start_position);

    Serial.print("  Pages      : ");
    Serial.println(region->pages);

    Serial.print("  Type       : ");
    if (region->type == MEMORY_REGION_IO)
      Serial.println("io");
    else
      Serial.println("normal");

    Serial.print("  Shared     : ");
    Serial.println(region->shared ? "true" : "false");

    Serial.print("  Trap write : ");
    Serial.println(region->trap_write ? "true" : "false");

    Serial.println();
  }

  Serial.println("--------------------------------------------------");
}

void dumpMMUPhysicalUsage()
{
  uint8_t maxPhys = 0;
  bool used[128];
  memset(used, 0, sizeof(used));

  int active = -1;

  for (int i = 0; i < MAX_MEMORY_MODELS; i++) {
    if (memoryConfig.models[i].name[0] &&
      strcmp(memoryConfig.models[i].name,
        memoryConfig.active_model) == 0) {
      active = i;
      break;
    }
  }

  if (active < 0) {
    Serial.println("*E: Active model not found");
    return;
  }

  MemoryModel* model = &memoryConfig.models[active];

  for (int ctx = 0; ctx < model->contexts; ctx++)
  {
    for (int page = 0; page < NUM_CONTEXT_PAGES; page++)
    {
      uint8_t phys = readMMUPage(ctx, page);
      uint8_t base = phys & 0x7F;   // mask IO bit

      used[base] = true;

      if (base > maxPhys)
        maxPhys = base;
    }
  }

  int totalUsed = 0;

  for (int i = 0; i < 128; i++)
    if (used[i])
      totalUsed++;

  Serial.println("--------------------------------------------------");
  Serial.println("MMU PHYSICAL PAGE USAGE");
  Serial.println("--------------------------------------------------");

  Serial.print("Highest page used : ");
  Serial.println(maxPhys);

  Serial.print("Total pages used  : ");
  Serial.print(totalUsed);
  Serial.print(" / 128 (");
  Serial.print((totalUsed * 100) / 128);
  Serial.println("%)");

  Serial.println("--------------------------------------------------");
}


// =====================================================
// MMU PAGE MAP DUMP
// =====================================================

void dumpMMUPageMap(const uint8_t context)
{
  if (context >= NUM_CONTEXTS) {
    Serial.println("*E: Invalid context");
    return;
  }

  Serial.println("--------------------------------------------------");
  Serial.print("MMU PAGE MAP - Context ");
  Serial.println(context);
  Serial.println("--------------------------------------------------");

  for (int page = 0; page < NUM_CONTEXT_PAGES; page++)
  {
    uint8_t phys = readMMUPage(context, page);

    uint8_t base = phys & 0x7F;
    bool io = (phys & 0x80) ? true : false;

    Serial.print("L");
    if (page < 10) Serial.print("0");
    Serial.print(page);
    Serial.print(" -> ");

    if (base < 10) Serial.print("00");
    else if (base < 100) Serial.print("0");

    Serial.print(base);

    if (io)
      Serial.print("  (IO)");

    Serial.println();
  }

  Serial.println("--------------------------------------------------");
}

void dumpMMUPageMapsCompact()
{
  int active = -1;

  for (int i = 0; i < MAX_MEMORY_MODELS; i++) {
    if (memoryConfig.models[i].name[0] &&
      strcmp(memoryConfig.models[i].name,
        memoryConfig.active_model) == 0) {
      active = i;
      break;
    }
  }

  if (active < 0) {
    Serial.println("*E: Active model not found");
    return;
  }

  MemoryModel* model = &memoryConfig.models[active];

  for (int ctx = 0; ctx < model->contexts; ctx++)
    dumpMMUContext(ctx);
}