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
#include "memory_config.h"
#include "mmu.h"

MemoryConfig memoryConfig;
MemoryIniError lastMemoryIniError;

static uint8_t phys_map[MAX_MEMORY_CONTEXTS][NUM_CONTEXT_PAGES];

struct SharedPage {
  char model[MAX_MEMORY_NAME];
  char region[MAX_MEMORY_NAME];
  uint8_t offset;
  uint8_t phys;
};

static SharedPage shared_pages[NUM_TOTAL_PAGES];
static uint8_t shared_page_count = 0;

/// <summary>
/// 
/// </summary>
static void clearMemoryIniError() {
  memset(&lastMemoryIniError, 0, sizeof(lastMemoryIniError));
  lastMemoryIniError.code = MEMORY_INI_OK;
}

/// <summary>
/// 
/// </summary>
/// <param name="code"></param>
/// <param name="line"></param>
/// <param name="section"></param>
/// <param name="key"></param>
/// <param name="value"></param>
/// <returns></returns>
static bool failMemoryIni(MemoryIniErrorCode code, int line, const char* section, const char* key, const char* value) {
  clearMemoryIniError();

  lastMemoryIniError.code = code;
  lastMemoryIniError.line = line;

  if (section)
    strncpy(lastMemoryIniError.section, section,
      sizeof(lastMemoryIniError.section) - 1);

  if (key)
    strncpy(lastMemoryIniError.key, key,
      sizeof(lastMemoryIniError.key) - 1);

  if (value)
    strncpy(lastMemoryIniError.value, value,
      sizeof(lastMemoryIniError.value) - 1);

  return false;
}

/// <summary>
/// 
/// </summary>
/// <param name="code"></param>
/// <returns></returns>
const char* memoryIniErrorToString(MemoryIniErrorCode code) {
  switch (code) {
  case MEMORY_INI_OK:
    return "ok";
  case MEMORY_INI_OPEN_FAILED:
    return "open failed";
  case MEMORY_INI_SYNTAX:
    return "syntax error";
  case MEMORY_INI_UNKNOWN_SECTION:
    return "unknown section";
  case MEMORY_INI_UNKNOWN_KEY:
    return "unknown key";
  case MEMORY_INI_DUPLICATE_MODEL:
    return "duplicate model";
  case MEMORY_INI_DUPLICATE_LAYOUT:
    return "duplicate layout";
  case MEMORY_INI_DUPLICATE_REGION:
    return "duplicate region";
  case MEMORY_INI_DUPLICATE_CONTEXT:
    return "duplicate context";
  case MEMORY_INI_INVALID_CONTEXT:
    return "invalid context";
  case MEMORY_INI_INVALID_VALUE:
    return "invalid value";
  case MEMORY_INI_INVALID_RANGE:
    return "invalid range";
  case MEMORY_INI_MODEL_NOT_FOUND:
    return "model not found";
  case MEMORY_INI_LAYOUT_NOT_FOUND:
    return "layout not found";
  case MEMORY_INI_LAYOUT_INCOMPLETE:
    return "layout incomplete";
  case MEMORY_INI_OVERLAPPING_REGIONS:
    return "overlapping regions";
  default:
    return "unknown error";
  }
}

/// <summary>
/// 
/// </summary>
/// <param name="s"></param>
/// <param name="out"></param>
/// <returns></returns>
static bool parseBoolValue(const char* s, bool* out) {
  if (strcmp(s, "true") == 0) {
    *out = true;
    return true;
  }

  if (strcmp(s, "false") == 0) {
    *out = false;
    return true;
  }

  return false;
}

/// <summary>
/// 
/// </summary>
/// <param name="s"></param>
/// <param name="out"></param>
/// <returns></returns>
static bool parseU8Strict(const char* s, uint8_t* out) {
  char* end;
  long v;

  if (!s || !*s)
    return false;

  v = strtol(s, &end, 10);

  if (*end != 0)
    return false;

  if (v < 0 || v > 255)
    return false;

  *out = (uint8_t)v;
  return true;
}

/// <summary>
/// 
/// </summary>
/// <param name="s"></param>
/// <param name="out"></param>
/// <returns></returns>
static bool parseContextIdStrict(const char* s, uint8_t* out) {
  return parseU8Strict(s, out) && (*out < MAX_MEMORY_CONTEXTS);
}

/// <summary>
/// 
/// </summary>
/// <param name="name"></param>
/// <returns></returns>
static MemoryModel* findModel(const char* name) {
  uint8_t i;

  for (i = 0; i < memoryConfig.model_count; i++) {
    if (strcmp(memoryConfig.models[i].name, name) == 0)
      return &memoryConfig.models[i];
  }

  return 0;
}

/// <summary>
/// 
/// </summary>
/// <param name="model"></param>
/// <param name="name"></param>
/// <returns></returns>
static MemoryLayout* findLayout(const char* model, const char* name) {
  uint8_t i;

  for (i = 0; i < memoryConfig.layout_count; i++) {
    if (strcmp(memoryConfig.layouts[i].model, model) == 0 &&
      strcmp(memoryConfig.layouts[i].name, name) == 0)
      return &memoryConfig.layouts[i];
  }

  return 0;
}

/// <summary>
/// 
/// </summary>
/// <param name="model"></param>
/// <param name="context"></param>
/// <returns></returns>
static MemoryContextBinding* findContextBinding(const char* model, uint8_t context) {
  uint8_t i;

  for (i = 0; i < memoryConfig.context_count; i++) {
    if (strcmp(memoryConfig.contexts[i].model, model) == 0 &&
      memoryConfig.contexts[i].context == context)
      return &memoryConfig.contexts[i];
  }

  return 0;
}

/// <summary>
/// 
/// </summary>
/// <param name="layout"></param>
/// <param name="name"></param>
/// <returns></returns>
static MemoryLayoutRegion* findRegion(MemoryLayout* layout, const char* name) {
  uint8_t i;

  for (i = 0; i < layout->region_count; i++) {
    if (strcmp(layout->regions[i].name, name) == 0)
      return &layout->regions[i];
  }

  return 0;
}

/// <summary>
/// 
/// </summary>
/// <param name="reg"></param>
/// <returns></returns>
static bool isValidRegionRange(const MemoryLayoutRegion* reg) {
  if (reg->pages == 0)
    return false;

  if (reg->start >= NUM_CONTEXT_PAGES)
    return false;

  if ((uint16_t)reg->start + (uint16_t)reg->pages > NUM_CONTEXT_PAGES)
    return false;

  return true;
}

/// <summary>
/// 
/// </summary>
/// <param name="layout"></param>
/// <returns></returns>
static bool layoutHasOverlaps(const MemoryLayout* layout) {
  uint8_t i;
  uint8_t j;

  for (i = 0; i < layout->region_count; i++) {
    uint8_t a0 = layout->regions[i].start;
    uint8_t a1 = a0 + layout->regions[i].pages;

    for (j = i + 1; j < layout->region_count; j++) {
      uint8_t b0 = layout->regions[j].start;
      uint8_t b1 = b0 + layout->regions[j].pages;

      if (a0 < b1 && b0 < a1)
        return true;
    }
  }

  return false;
}

/// <summary>
/// 
/// </summary>
/// <param name="layout"></param>
/// <returns></returns>
static bool layoutIsComplete(const MemoryLayout* layout)
{
  uint8_t covered[NUM_CONTEXT_PAGES];
  uint8_t i;
  uint8_t p;

  memset(covered, 0, sizeof(covered));

  for (i = 0; i < layout->region_count; i++) {
    for (p = 0; p < layout->regions[i].pages; p++)
      covered[layout->regions[i].start + p] = 1;
  }

  for (i = 0; i < NUM_CONTEXT_PAGES; i++) {
    if (!covered[i])
      return false;
  }

  return true;
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
static bool validateConfig() {
  uint8_t i;
  uint8_t j;
  MemoryModel* active;

  if (memoryConfig.version == 0) {
    return failMemoryIni(MEMORY_INI_INVALID_VALUE, 0,
      "system", "config_version", 0);
  }

  if (memoryConfig.active_model[0] == 0) {
    return failMemoryIni(MEMORY_INI_INVALID_VALUE, 0,
      "system", "active_model", 0);
  }

  active = findModel(memoryConfig.active_model);
  if (!active) {
    return failMemoryIni(MEMORY_INI_MODEL_NOT_FOUND, 0,
      "system", "active_model", memoryConfig.active_model);
  }

  if (memoryConfig.boot_context >= active->contexts) {
    return failMemoryIni(MEMORY_INI_INVALID_CONTEXT, 0,
      "system", "boot_context", 0);
  }

  for (i = 0; i < memoryConfig.model_count; i++) {
    MemoryModel* model = &memoryConfig.models[i];

    if (model->contexts == 0 || model->contexts > MAX_MEMORY_CONTEXTS) {
      return failMemoryIni(MEMORY_INI_INVALID_VALUE, 0,
        model->name, "contexts", 0);
    }
  }

  for (i = 0; i < memoryConfig.layout_count; i++) {
    MemoryLayout* layout = &memoryConfig.layouts[i];

    if (!findModel(layout->model)) {
      return failMemoryIni(MEMORY_INI_MODEL_NOT_FOUND, 0,
        layout->name, "model", layout->model);
    }

    if (layout->region_count == 0) {
      return failMemoryIni(MEMORY_INI_LAYOUT_INCOMPLETE, 0,
        layout->name, 0, 0);
    }

    for (j = 0; j < layout->region_count; j++) {
      if (!isValidRegionRange(&layout->regions[j])) {
        return failMemoryIni(MEMORY_INI_INVALID_RANGE, 0,
          layout->name, layout->regions[j].name, 0);
      }
    }

    if (layoutHasOverlaps(layout)) {
      return failMemoryIni(MEMORY_INI_OVERLAPPING_REGIONS, 0,
        layout->name, 0, 0);
    }

    if (!layoutIsComplete(layout)) {
      return failMemoryIni(MEMORY_INI_LAYOUT_INCOMPLETE, 0,
        layout->name, 0, 0);
    }
  }

  for (i = 0; i < active->contexts; i++) {
    MemoryContextBinding* ctx = findContextBinding(active->name, i);

    if (!ctx) {
      char num[8];
      snprintf(num, sizeof(num), "%u", i);
      return failMemoryIni(MEMORY_INI_INVALID_CONTEXT, 0,
        "context", "missing", num);
    }

    if (!findLayout(active->name, ctx->layout)) {
      return failMemoryIni(MEMORY_INI_LAYOUT_NOT_FOUND, 0,
        "context", "layout", ctx->layout);
    }
  }

  return true;
}

/// <summary>
/// 
/// </summary>
/// <param name="file"></param>
/// <returns></returns>
bool parseMemoryIni(File& file) {
  char line[160];
  char section[64];
  int lineNo = 0;

  MemoryLayout* currentLayout = 0;
  MemoryLayoutRegion* currentRegion = 0;

  memset(&memoryConfig, 0, sizeof(memoryConfig));
  memset(section, 0, sizeof(section));
  clearMemoryIniError();

  while (file.available()) {
    size_t len = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[len] = 0;
    lineNo++;

    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
      line[len - 1] = 0;
      len--;
    }

    if (line[0] == 0 || line[0] == '#' || line[0] == ';')
      continue;

    if (line[0] == '[') {
      char* end = strchr(line, ']');

      if (!end)
        return failMemoryIni(MEMORY_INI_SYNTAX, lineNo, 0, 0, line);

      *end = 0;
      strncpy(section, line + 1, sizeof(section) - 1);
      section[sizeof(section) - 1] = 0;

      currentLayout = 0;
      currentRegion = 0;

      if (strcmp(section, "system") == 0)
        continue;

      if (strncmp(section, "model.", 6) == 0) {
        char name[32];

        strncpy(name, section + 6, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;

        if (findModel(name)) {
          return failMemoryIni(MEMORY_INI_DUPLICATE_MODEL, lineNo,
            section, "model", name);
        }

        if (memoryConfig.model_count >= MAX_MEMORY_MODELS) {
          return failMemoryIni(MEMORY_INI_INVALID_VALUE, lineNo,
            section, "model_count", name);
        }

        memset(&memoryConfig.models[memoryConfig.model_count], 0,
          sizeof(memoryConfig.models[memoryConfig.model_count]));
        strncpy(memoryConfig.models[memoryConfig.model_count].name, name,
          sizeof(memoryConfig.models[memoryConfig.model_count].name) - 1);
        memoryConfig.model_count++;
        continue;
      }

      if (strncmp(section, "layout.", 7) == 0) {
        char temp[64];
        char* tok;
        char* saveptr = 0;
        char* parts[5];
        int count = 0;
        char modelName[32];
        char layoutName[32];
        char regionName[32];

        memset(temp, 0, sizeof(temp));
        strncpy(temp, section, sizeof(temp) - 1);

        tok = strtok_r(temp, ".", &saveptr);
        while (tok && count < 5) {
          parts[count++] = tok;
          tok = strtok_r(0, ".", &saveptr);
        }

        if (count != 5) {
          return failMemoryIni(MEMORY_INI_UNKNOWN_SECTION, lineNo,
            section, 0, 0);
        }

        if (strcmp(parts[0], "layout") != 0 ||
          strcmp(parts[3], "region") != 0) {
          return failMemoryIni(MEMORY_INI_UNKNOWN_SECTION, lineNo,
            section, 0, 0);
        }

        strncpy(modelName, parts[1], sizeof(modelName) - 1);
        modelName[sizeof(modelName) - 1] = 0;

        strncpy(layoutName, parts[2], sizeof(layoutName) - 1);
        layoutName[sizeof(layoutName) - 1] = 0;

        strncpy(regionName, parts[4], sizeof(regionName) - 1);
        regionName[sizeof(regionName) - 1] = 0;

        currentLayout = findLayout(modelName, layoutName);
        if (!currentLayout) {
          if (memoryConfig.layout_count >= MAX_MEMORY_LAYOUTS) {
            return failMemoryIni(MEMORY_INI_INVALID_VALUE, lineNo,
              section, "layout_count", layoutName);
          }

          currentLayout = &memoryConfig.layouts[memoryConfig.layout_count++];
          memset(currentLayout, 0, sizeof(*currentLayout));
          strncpy(currentLayout->model, modelName,
            sizeof(currentLayout->model) - 1);
          strncpy(currentLayout->name, layoutName,
            sizeof(currentLayout->name) - 1);
        }

        if (findRegion(currentLayout, regionName)) {
          return failMemoryIni(MEMORY_INI_DUPLICATE_REGION, lineNo,
            section, "region", regionName);
        }

        if (currentLayout->region_count >= MAX_MEMORY_REGIONS) {
          return failMemoryIni(MEMORY_INI_INVALID_VALUE, lineNo,
            section, "region_count", regionName);
        }

        currentRegion = &currentLayout->regions[currentLayout->region_count++];
        memset(currentRegion, 0, sizeof(*currentRegion));
        strncpy(currentRegion->name, regionName,
          sizeof(currentRegion->name) - 1);
        continue;
      }

      if (strncmp(section, "context.", 8) == 0) {
        char temp[64];
        char* tok;
        char* saveptr = 0;
        char* parts[3];
        int count = 0;
        uint8_t ctx;

        memset(temp, 0, sizeof(temp));
        strncpy(temp, section, sizeof(temp) - 1);

        tok = strtok_r(temp, ".", &saveptr);
        while (tok && count < 3) {
          parts[count++] = tok;
          tok = strtok_r(0, ".", &saveptr);
        }

        if (count != 3 || strcmp(parts[0], "context") != 0) {
          return failMemoryIni(MEMORY_INI_UNKNOWN_SECTION, lineNo,
            section, 0, 0);
        }

        if (!parseContextIdStrict(parts[2], &ctx)) {
          return failMemoryIni(MEMORY_INI_INVALID_CONTEXT, lineNo,
            section, "context", parts[2]);
        }

        if (findContextBinding(parts[1], ctx)) {
          return failMemoryIni(MEMORY_INI_DUPLICATE_CONTEXT, lineNo,
            section, "context", parts[2]);
        }

        if (memoryConfig.context_count >= MAX_MEMORY_CONTEXTS) {
          return failMemoryIni(MEMORY_INI_INVALID_VALUE, lineNo,
            section, "context_count", 0);
        }

        memset(&memoryConfig.contexts[memoryConfig.context_count], 0,
          sizeof(memoryConfig.contexts[memoryConfig.context_count]));
        strncpy(memoryConfig.contexts[memoryConfig.context_count].model,
          parts[1], sizeof(memoryConfig.contexts[memoryConfig.context_count].model) - 1);
        memoryConfig.contexts[memoryConfig.context_count].context = ctx;
        memoryConfig.context_count++;
        continue;
      }

      return failMemoryIni(MEMORY_INI_UNKNOWN_SECTION, lineNo,
        section, 0, 0);
    }

    {
      char* eq = strchr(line, '=');
      char* key;
      char* val;

      if (!eq)
        return failMemoryIni(MEMORY_INI_SYNTAX, lineNo, section, 0, line);

      *eq = 0;
      key = line;
      val = eq + 1;

      while (*key == ' ' || *key == '\t')
        key++;

      while (*val == ' ' || *val == '\t')
        val++;

      {
        char* p = key + strlen(key);
        while (p > key && (p[-1] == ' ' || p[-1] == '\t')) {
          p--;
          *p = 0;
        }
      }

      {
        char* p = val + strlen(val);
        while (p > val && (p[-1] == ' ' || p[-1] == '\t')) {
          p--;
          *p = 0;
        }
      }

      if (strcmp(section, "system") == 0) {
        uint8_t temp;

        if (strcmp(key, "config_version") == 0) {
          if (!parseU8Strict(val, &temp))
            return failMemoryIni(MEMORY_INI_INVALID_VALUE, lineNo, section, key, val);

          memoryConfig.version = temp;
        }
        else if (strcmp(key, "active_model") == 0) {
          strncpy(memoryConfig.active_model, val,
            sizeof(memoryConfig.active_model) - 1);
        }
        else if (strcmp(key, "boot_context") == 0) {
          if (!parseContextIdStrict(val, &temp))
            return failMemoryIni(MEMORY_INI_INVALID_CONTEXT, lineNo, section, key, val);

          memoryConfig.boot_context = temp;
        }
        else {
          return failMemoryIni(MEMORY_INI_UNKNOWN_KEY, lineNo,
            section, key, val);
        }
      }
      else if (strncmp(section, "model.", 6) == 0) {
        char modelName[32];
        MemoryModel* model;

        strncpy(modelName, section + 6, sizeof(modelName) - 1);
        modelName[sizeof(modelName) - 1] = 0;
        model = findModel(modelName);

        if (!model) {
          return failMemoryIni(MEMORY_INI_MODEL_NOT_FOUND, lineNo,
            section, "model", modelName);
        }

        if (strcmp(key, "contexts") == 0) {
          uint8_t temp;

          if (!parseU8Strict(val, &temp))
            return failMemoryIni(MEMORY_INI_INVALID_VALUE, lineNo, section, key, val);

          model->contexts = temp;
        }
        else {
          return failMemoryIni(MEMORY_INI_UNKNOWN_KEY, lineNo,
            section, key, val);
        }
      }
      else if (currentRegion) {
        if (strcmp(key, "start") == 0) {
          uint8_t temp;

          if (!parseU8Strict(val, &temp))
            return failMemoryIni(MEMORY_INI_INVALID_VALUE, lineNo, section, key, val);

          currentRegion->start = temp;
        }
        else if (strcmp(key, "pages") == 0) {
          uint8_t temp;

          if (!parseU8Strict(val, &temp))
            return failMemoryIni(MEMORY_INI_INVALID_VALUE, lineNo, section, key, val);

          currentRegion->pages = temp;
        }
        else if (strcmp(key, "shared") == 0) {
          if (!parseBoolValue(val, &currentRegion->shared))
            return failMemoryIni(MEMORY_INI_INVALID_VALUE, lineNo, section, key, val);
        }
        else if (strcmp(key, "trap_write") == 0) {
          if (!parseBoolValue(val, &currentRegion->trap_write))
            return failMemoryIni(MEMORY_INI_INVALID_VALUE, lineNo, section, key, val);
        }
        else if (strcmp(key, "type") == 0) {
          if (strcmp(val, "io") == 0)
            currentRegion->type = MEMORY_REGION_IO;
          else if (strcmp(val, "normal") == 0)
            currentRegion->type = MEMORY_REGION_NORMAL;
          else
            return failMemoryIni(MEMORY_INI_INVALID_VALUE, lineNo, section, key, val);
        }
        else {
          return failMemoryIni(MEMORY_INI_UNKNOWN_KEY, lineNo,
            section, key, val);
        }
      }
      else if (strncmp(section, "context.", 8) == 0) {
        char temp[64];
        char* tok;
        char* saveptr = 0;
        char* parts[3];
        int count = 0;
        uint8_t ctx;
        MemoryContextBinding* binding;

        memset(temp, 0, sizeof(temp));
        strncpy(temp, section, sizeof(temp) - 1);

        tok = strtok_r(temp, ".", &saveptr);
        while (tok && count < 3) {
          parts[count++] = tok;
          tok = strtok_r(0, ".", &saveptr);
        }

        if (count != 3 || strcmp(parts[0], "context") != 0) {
          return failMemoryIni(MEMORY_INI_UNKNOWN_SECTION, lineNo,
            section, 0, 0);
        }

        if (!parseContextIdStrict(parts[2], &ctx)) {
          return failMemoryIni(MEMORY_INI_INVALID_CONTEXT, lineNo,
            section, "context", parts[2]);
        }

        binding = findContextBinding(parts[1], ctx);
        if (!binding) {
          return failMemoryIni(MEMORY_INI_INVALID_CONTEXT, lineNo,
            section, "context", parts[2]);
        }

        if (strcmp(key, "layout") == 0) {
          strncpy(binding->layout, val, sizeof(binding->layout) - 1);
        }
        else {
          return failMemoryIni(MEMORY_INI_UNKNOWN_KEY, lineNo,
            section, key, val);
        }
      }
      else {
        return failMemoryIni(MEMORY_INI_UNKNOWN_SECTION, lineNo,
          section, key, val);
      }
    }
  }

  return validateConfig();
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
bool initializeMemoryConfig() {
  File file;

  memset(&memoryConfig, 0, sizeof(memoryConfig));
  clearMemoryIniError();

  file = LittleFS.open("/memory.ini", "r");

  if (!file) {
    lastMemoryIniError.code = MEMORY_INI_OPEN_FAILED;
    Serial1.println("*E: MEMORY.INI missing");
    return false;
  }

  if (!parseMemoryIni(file)) {
    file.close();

    Serial1.printf("*E: MEMORY.INI error: %s (code=%d, line=%d)\n",
      memoryIniErrorToString(lastMemoryIniError.code),
      lastMemoryIniError.code,
      lastMemoryIniError.line);

    if (lastMemoryIniError.section[0]) {
      Serial1.print("*E: Section: ");
      Serial1.println(lastMemoryIniError.section);
    }

    if (lastMemoryIniError.key[0]) {
      Serial1.print("*E: Key    : ");
      Serial1.println(lastMemoryIniError.key);
    }

    if (lastMemoryIniError.value[0]) {
      Serial1.print("*E: Value  : ");
      Serial1.println(lastMemoryIniError.value);
    }

    return false;
  }

  file.close();
  Serial1.println("*I: Memory config loaded.");
  return true;
}

static MemoryLayout* resolveLayoutForContext(MemoryModel* model, uint8_t ctx) {
  MemoryContextBinding* binding = findContextBinding(model->name, ctx);

  if (!binding)
    return 0;

  return findLayout(model->name, binding->layout);
}

static uint8_t getSharedPhysical(const char* model, const char* region,
  uint8_t offset, uint8_t* nextPhysical)
{
  uint8_t i;

  for (i = 0; i < shared_page_count; i++) {
    if (strcmp(shared_pages[i].model, model) == 0 &&
      strcmp(shared_pages[i].region, region) == 0 &&
      shared_pages[i].offset == offset)
      return shared_pages[i].phys;
  }

  if (*nextPhysical >= NUM_TOTAL_PAGES || shared_page_count >= NUM_TOTAL_PAGES)
    return 0xFF;

  memset(&shared_pages[shared_page_count], 0, sizeof(shared_pages[shared_page_count]));
  strncpy(shared_pages[shared_page_count].model, model,
    sizeof(shared_pages[shared_page_count].model) - 1);
  strncpy(shared_pages[shared_page_count].region, region,
    sizeof(shared_pages[shared_page_count].region) - 1);
  shared_pages[shared_page_count].offset = offset;
  shared_pages[shared_page_count].phys = *nextPhysical;

  shared_page_count++;
  return (*nextPhysical)++;
}

bool configureMMUFromActiveModel() {
  MemoryModel* model;
  uint8_t nextPhysical = 0;
  uint8_t ctx;

  model = findModel(memoryConfig.active_model);
  if (!model)
    return false;

  if (model->contexts == 0 || model->contexts > MAX_MEMORY_CONTEXTS)
    return false;

  memset(phys_map, 0, sizeof(phys_map));
  memset(shared_pages, 0, sizeof(shared_pages));
  shared_page_count = 0;

  for (ctx = 0; ctx < model->contexts; ctx++) {
    MemoryLayout* layout = resolveLayoutForContext(model, ctx);
    uint8_t r;

    if (!layout)
      return false;

    for (r = 0; r < layout->region_count; r++) {
      MemoryLayoutRegion* region = &layout->regions[r];
      uint8_t p;

      for (p = 0; p < region->pages; p++) {
        uint8_t phys;
        uint8_t logical = region->start + p;

        if (region->shared) {
          phys = getSharedPhysical(model->name, region->name, p, &nextPhysical);
          if (phys == 0xFF)
            return false;
        }
        else {
          if (nextPhysical >= NUM_TOTAL_PAGES)
            return false;

          phys = nextPhysical++;
        }

        phys_map[ctx][logical] = phys;
      }
    }
  }

  for (ctx = 0; ctx < model->contexts; ctx++) {
    MemoryLayout* layout = resolveLayoutForContext(model, ctx);
    uint8_t slot;

    if (!layout)
      return false;

    for (slot = 0; slot < NUM_CONTEXT_PAGES; slot++) {
      uint8_t phys = phys_map[ctx][slot];
      uint8_t r;
      bool found = false;

      for (r = 0; r < layout->region_count; r++) {
        MemoryLayoutRegion* region = &layout->regions[r];

        if (slot >= region->start && slot < region->start + region->pages) {
          if (region->type == MEMORY_REGION_IO || region->trap_write)
            phys |= 0x80;

          found = true;
          break;
        }
      }

      if (!found)
        return false;

      if (!writeMMUPage(ctx, slot, phys))
        return false;
    }
  }

  return true;
}

/// <summary>
/// 
/// </summary>
void dumpMemoryConfig() {
  uint8_t i;
  uint8_t r;

  Serial1.println("--------------------------------------------------");
  Serial1.println("MEMORY CONFIG");
  Serial1.println("--------------------------------------------------");

  Serial1.printf("Version      : %d\n", memoryConfig.version);

  Serial1.print("Active model : ");
  Serial1.println(memoryConfig.active_model);

  Serial1.printf("Boot context : %X\n\n", memoryConfig.boot_context);

  for (i = 0; i < memoryConfig.model_count; i++) {
    Serial1.print("Model: ");
    Serial1.println(memoryConfig.models[i].name);

    Serial1.printf("  Contexts : %d\n\n", memoryConfig.models[i].contexts);
  }

  for (i = 0; i < memoryConfig.layout_count; i++) {
    MemoryLayout* layout = &memoryConfig.layouts[i];

    Serial1.print("Layout: ");
    Serial1.print(layout->model);
    Serial1.print(" / ");
    Serial1.println(layout->name);

    for (r = 0; r < layout->region_count; r++) {
      MemoryLayoutRegion* region = &layout->regions[r];

      Serial1.print("  Region      : ");
      Serial1.println(region->name);

      Serial1.printf("    Start     : %02X\n", region->start);

      Serial1.printf("    Pages     : %d\n", region->pages);

      Serial1.print("    Type      : ");
      if (region->type == MEMORY_REGION_IO)
        Serial1.println("io");
      else
        Serial1.println("normal");

      Serial1.print("    Shared    : ");
      Serial1.println(region->shared ? "true" : "false");

      Serial1.print("    Trap write: ");
      Serial1.println(region->trap_write ? "true" : "false");
    }

    Serial1.println();
  }
}

/// <summary>
/// 
/// </summary>
void dumpMMUPhysicalUsage() {
  MemoryModel* model;
  bool used[NUM_TOTAL_PAGES];
  uint8_t maxPhys = 0;
  int totalUsed = 0;
  uint8_t ctx;
  uint8_t page;
  uint8_t i;

  model = findModel(memoryConfig.active_model);
  if (!model) {
    Serial1.println("*E: Active model not found");
    return;
  }

  memset(used, 0, sizeof(used));

  for (ctx = 0; ctx < model->contexts; ctx++) {
    for (page = 0; page < NUM_CONTEXT_PAGES; page++) {
      uint8_t phys = readMMUPage(ctx, page);
      uint8_t base = phys & 0x7F;

      used[base] = true;

      if (base > maxPhys)
        maxPhys = base;
    }
  }

  for (i = 0; i < NUM_TOTAL_PAGES; i++) {
    if (used[i])
      totalUsed++;
  }

  Serial1.println("--------------------------------------------------");
  Serial1.println("MMU PHYSICAL PAGE USAGE");
  Serial1.println("--------------------------------------------------");

  Serial1.printf("Highest page used : %02X\n", maxPhys);

  Serial1.printf("Total pages used  : %d / %d ( %d%% )\n", totalUsed, NUM_TOTAL_PAGES, (totalUsed * 100) / NUM_TOTAL_PAGES);

  Serial1.println("--------------------------------------------------");
}

/// <summary>
/// 
/// </summary>
/// <param name="context"></param>
void dumpMMUPageMap(uint8_t context) {
  uint8_t page;

  if (context >= MAX_MEMORY_CONTEXTS) {
    Serial1.println("*E: Invalid context");
    return;
  }

  Serial1.println("--------------------------------------------------");
  Serial1.printf("MMU PAGE MAP - Context %d\n", context);
  Serial1.println("--------------------------------------------------");

  for (page = 0; page < NUM_CONTEXT_PAGES; page++) {
    uint8_t phys = readMMUPage(context, page);
    uint8_t base = phys & 0x7F;
    bool io = (phys & 0x80) ? true : false;

    Serial1.printf("P%02X -> %02X", page, base);

    if (io)
      Serial1.print(" (IO)");

    Serial1.println();
  }

  Serial1.println("--------------------------------------------------");
}

/// <summary>
/// 
/// </summary>
/// <param name="context"></param>
void dumpMMUContext(uint8_t context) {
  uint8_t page;

  if (context >= MAX_MEMORY_CONTEXTS)
    return;

  Serial1.printf("CTX %X : ", context);

  for (page = 0; page < NUM_CONTEXT_PAGES; page++) {
    uint8_t phys = readMMUPage(context, page);
    uint8_t base = phys & 0x7F;
    bool io = (phys & 0x80) ? true : false;

    if (page)
      Serial1.print(" ");

    Serial1.printf("%02X", base);

    if (io)
      Serial1.print("*");
    else
      Serial1.print(" ");
  }

  Serial1.println();
}

/// <summary>
/// 
/// </summary>
void dumpMMUPageMapsCompact() {
  MemoryModel* model;
  uint8_t ctx;

  model = findModel(memoryConfig.active_model);
  if (!model) {
    Serial1.println("*E: Active model not found");
    return;
  }

  Serial1.println("--------------------------------------------------");
  Serial1.println("MMU PAGE MAPS");
  Serial1.println("--------------------------------------------------");

  for (ctx = 0; ctx < model->contexts; ctx++)
    dumpMMUContext(ctx);

  Serial1.println("--------------------------------------------------");
}
