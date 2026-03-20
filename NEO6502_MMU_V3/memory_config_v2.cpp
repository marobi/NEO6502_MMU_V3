// memory_config.cpp
#include "memory_config_v2.h"
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

static void clearMemoryIniError()
{
  memset(&lastMemoryIniError, 0, sizeof(lastMemoryIniError));
  lastMemoryIniError.code = MEMORY_INI_OK;
}

static bool failMemoryIni(MemoryIniErrorCode code, int line,
  const char* section, const char* key, const char* value)
{
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

const char* memoryIniErrorToString(MemoryIniErrorCode code)
{
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
  case MEMORY_INI_INVALID_LAYOUT_CLASS:
    return "invalid layout class";
  case MEMORY_INI_MODEL_NOT_FOUND:
    return "model not found";
  case MEMORY_INI_LAYOUT_NOT_FOUND:
    return "layout not found";
  case MEMORY_INI_LAYOUT_MODEL_MISMATCH:
    return "layout model mismatch";
  case MEMORY_INI_LAYOUT_INCOMPLETE:
    return "layout incomplete";
  case MEMORY_INI_OVERLAPPING_REGIONS:
    return "overlapping regions";
  default:
    return "unknown error";
  }
}

static bool parseBoolValue(const char* s, bool* out)
{
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

static bool parseU8Strict(const char* s, uint8_t* out)
{
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

static bool parseContextIdStrict(const char* s, uint8_t* out)
{
  return parseU8Strict(s, out) && (*out < MAX_MEMORY_CONTEXTS);
}

static MemoryModel* findModel(const char* name)
{
  uint8_t i;

  for (i = 0; i < memoryConfig.model_count; i++) {
    if (strcmp(memoryConfig.models[i].name, name) == 0)
      return &memoryConfig.models[i];
  }

  return 0;
}

static MemoryLayout* findLayout(const char* model, const char* name)
{
  uint8_t i;

  for (i = 0; i < memoryConfig.layout_count; i++) {
    if (strcmp(memoryConfig.layouts[i].model, model) == 0 &&
      strcmp(memoryConfig.layouts[i].name, name) == 0)
      return &memoryConfig.layouts[i];
  }

  return 0;
}

static MemoryContextBinding* findContextBinding(const char* model, uint8_t context)
{
  uint8_t i;

  for (i = 0; i < memoryConfig.context_count; i++) {
    if (strcmp(memoryConfig.contexts[i].model, model) == 0 &&
      memoryConfig.contexts[i].context == context)
      return &memoryConfig.contexts[i];
  }

  return 0;
}

static MemoryLayoutRegion* findRegion(MemoryLayout* layout, const char* name)
{
  uint8_t i;

  for (i = 0; i < layout->region_count; i++) {
    if (strcmp(layout->regions[i].name, name) == 0)
      return &layout->regions[i];
  }

  return 0;
}

static bool isValidRegionRange(const MemoryLayoutRegion* reg)
{
  if (reg->pages == 0)
    return false;

  if (reg->start >= NUM_CONTEXT_PAGES)
    return false;

  if ((uint16_t)reg->start + (uint16_t)reg->pages > NUM_CONTEXT_PAGES)
    return false;

  return true;
}

static bool layoutHasOverlaps(const MemoryLayout* layout)
{
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

static bool validateConfig()
{
  uint8_t i;
  uint8_t j;

  if (memoryConfig.version == 0) {
    return failMemoryIni(MEMORY_INI_INVALID_VALUE, 0,
      "system", "config_version", 0);
  }

  if (memoryConfig.active_model[0] == 0) {
    return failMemoryIni(MEMORY_INI_INVALID_VALUE, 0,
      "system", "active_model", 0);
  }

  if (!findModel(memoryConfig.active_model)) {
    return failMemoryIni(MEMORY_INI_MODEL_NOT_FOUND, 0,
      "system", "active_model", memoryConfig.active_model);
  }

  if (memoryConfig.boot_context >= MAX_MEMORY_CONTEXTS) {
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

  if (memoryConfig.default_system_layout[0]) {
    MemoryLayout* layout = findLayout(memoryConfig.active_model,
      memoryConfig.default_system_layout);

    if (!layout) {
      return failMemoryIni(MEMORY_INI_LAYOUT_NOT_FOUND, 0,
        "system", "default_system_layout",
        memoryConfig.default_system_layout);
    }

    if (layout->layout_class != MEMORY_LAYOUT_SYSTEM) {
      return failMemoryIni(MEMORY_INI_INVALID_LAYOUT_CLASS, 0,
        "system", "default_system_layout",
        memoryConfig.default_system_layout);
    }
  }

  if (memoryConfig.default_user_layout[0]) {
    MemoryLayout* layout = findLayout(memoryConfig.active_model,
      memoryConfig.default_user_layout);

    if (!layout) {
      return failMemoryIni(MEMORY_INI_LAYOUT_NOT_FOUND, 0,
        "system", "default_user_layout",
        memoryConfig.default_user_layout);
    }

    if (layout->layout_class != MEMORY_LAYOUT_USER) {
      return failMemoryIni(MEMORY_INI_INVALID_LAYOUT_CLASS, 0,
        "system", "default_user_layout",
        memoryConfig.default_user_layout);
    }
  }

  for (i = 0; i < memoryConfig.context_count; i++) {
    MemoryContextBinding* ctx = &memoryConfig.contexts[i];
    MemoryModel* model = findModel(ctx->model);
    MemoryLayout* layout;

    if (!model) {
      return failMemoryIni(MEMORY_INI_MODEL_NOT_FOUND, 0,
        "context", "model", ctx->model);
    }

    if (ctx->context >= model->contexts) {
      return failMemoryIni(MEMORY_INI_INVALID_CONTEXT, 0,
        "context", "context", 0);
    }

    layout = findLayout(ctx->model, ctx->layout);
    if (!layout) {
      return failMemoryIni(MEMORY_INI_LAYOUT_NOT_FOUND, 0,
        "context", "layout", ctx->layout);
    }
  }

  {
    MemoryModel* active = findModel(memoryConfig.active_model);

    if (!active)
      return false;

    if (memoryConfig.boot_context >= active->contexts) {
      return failMemoryIni(MEMORY_INI_INVALID_CONTEXT, 0,
        "system", "boot_context", 0);
    }
  }

  return true;
}

bool parseMemoryIni(File& file)
{
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
        char* parts[6];
        int count = 0;
        MemoryLayoutClass layoutClass;
        char modelName[32];
        char layoutName[32];
        char regionName[32];

        memset(temp, 0, sizeof(temp));
        strncpy(temp, section, sizeof(temp) - 1);

        tok = strtok_r(temp, ".", &saveptr);
        while (tok && count < 6) {
          parts[count++] = tok;
          tok = strtok_r(0, ".", &saveptr);
        }

        if (count != 6) {
          return failMemoryIni(MEMORY_INI_UNKNOWN_SECTION, lineNo,
            section, 0, 0);
        }

        if (strcmp(parts[0], "layout") != 0 || strcmp(parts[4], "region") != 0) {
          return failMemoryIni(MEMORY_INI_UNKNOWN_SECTION, lineNo,
            section, 0, 0);
        }

        strncpy(modelName, parts[1], sizeof(modelName) - 1);
        modelName[sizeof(modelName) - 1] = 0;

        if (strcmp(parts[2], "system") == 0)
          layoutClass = MEMORY_LAYOUT_SYSTEM;
        else if (strcmp(parts[2], "user") == 0)
          layoutClass = MEMORY_LAYOUT_USER;
        else
          return failMemoryIni(MEMORY_INI_INVALID_LAYOUT_CLASS, lineNo,
            section, 0, parts[2]);

        strncpy(layoutName, parts[3], sizeof(layoutName) - 1);
        layoutName[sizeof(layoutName) - 1] = 0;

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
          currentLayout->layout_class = layoutClass;
        }
        else {
          if (currentLayout->layout_class != layoutClass) {
            return failMemoryIni(MEMORY_INI_LAYOUT_MODEL_MISMATCH, lineNo,
              section, "layout_class", parts[2]);
          }
        }

        strncpy(regionName, parts[5], sizeof(regionName) - 1);
        regionName[sizeof(regionName) - 1] = 0;

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
        memoryConfig.contexts[memoryConfig.context_count].defined = true;
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
        else if (strcmp(key, "default_system_layout") == 0) {
          strncpy(memoryConfig.default_system_layout, val,
            sizeof(memoryConfig.default_system_layout) - 1);
        }
        else if (strcmp(key, "default_user_layout") == 0) {
          strncpy(memoryConfig.default_user_layout, val,
            sizeof(memoryConfig.default_user_layout) - 1);
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

bool initializeMemoryConfig()
{
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

static MemoryLayout* resolveLayoutForContext(MemoryModel* model, uint8_t ctx)
{
  MemoryContextBinding* binding = findContextBinding(model->name, ctx);

  if (binding)
    return findLayout(model->name, binding->layout);

  if (ctx == memoryConfig.boot_context)
    return findLayout(model->name, memoryConfig.default_system_layout);

  return findLayout(model->name, memoryConfig.default_user_layout);
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

  if (*nextPhysical >= NUM_TOTAL_PAGES)
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

bool configureMMUFromActiveModel()
{
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

void dumpMemoryConfig()
{
  uint8_t i;
  uint8_t r;

  Serial1.println("--------------------------------------------------");
  Serial1.println("MEMORY CONFIG");
  Serial1.println("--------------------------------------------------");

  Serial1.print("Version               : ");
  Serial1.println(memoryConfig.version);

  Serial1.print("Active model          : ");
  Serial1.println(memoryConfig.active_model);

  Serial1.print("Boot context          : ");
  Serial1.println(memoryConfig.boot_context);

  Serial1.print("Default system layout : ");
  Serial1.println(memoryConfig.default_system_layout);

  Serial1.print("Default user layout   : ");
  Serial1.println(memoryConfig.default_user_layout);
  Serial1.println();

  for (i = 0; i < memoryConfig.model_count; i++) {
    Serial1.print("Model: ");
    Serial1.println(memoryConfig.models[i].name);

    Serial1.print("  Contexts : ");
    Serial1.println(memoryConfig.models[i].contexts);
    Serial1.println();
  }

  for (i = 0; i < memoryConfig.layout_count; i++) {
    MemoryLayout* layout = &memoryConfig.layouts[i];

    Serial1.print("Layout: ");
    Serial1.print(layout->model);
    Serial1.print(" / ");
    if (layout->layout_class == MEMORY_LAYOUT_SYSTEM)
      Serial1.print("system");
    else
      Serial1.print("user");
    Serial1.print(" / ");
    Serial1.println(layout->name);

    for (r = 0; r < layout->region_count; r++) {
      MemoryLayoutRegion* region = &layout->regions[r];

      Serial1.print("  Region      : ");
      Serial1.println(region->name);

      Serial1.print("    Start     : ");
      Serial1.println(region->start);

      Serial1.print("    Pages     : ");
      Serial1.println(region->pages);

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

void dumpMMUPhysicalUsage()
{
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

  Serial1.print("Highest page used : ");
  Serial1.println(maxPhys);

  Serial1.print("Total pages used  : ");
  Serial1.print(totalUsed);
  Serial1.print(" / ");
  Serial1.print(NUM_TOTAL_PAGES);
  Serial1.print(" (");
  Serial1.print((totalUsed * 100) / NUM_TOTAL_PAGES);
  Serial1.println("%)");

  Serial1.println("--------------------------------------------------");
}

void dumpMMUPageMap(uint8_t context)
{
  uint8_t page;

  if (context >= MAX_MEMORY_CONTEXTS) {
    Serial1.println("*E: Invalid context");
    return;
  }

  Serial1.println("--------------------------------------------------");
  Serial1.print("MMU PAGE MAP - Context ");
  Serial1.println(context);
  Serial1.println("--------------------------------------------------");

  for (page = 0; page < NUM_CONTEXT_PAGES; page++) {
    uint8_t phys = readMMUPage(context, page);
    uint8_t base = phys & 0x7F;
    bool io = (phys & 0x80) ? true : false;

    Serial1.print("L");
    if (page < 10)
      Serial1.print("0");
    Serial1.print(page);
    Serial1.print(" -> ");

    if (base < 10)
      Serial1.print("00");
    else if (base < 100)
      Serial1.print("0");

    Serial1.print(base);

    if (io)
      Serial1.print("  (IO)");

    Serial1.println();
  }

  Serial1.println("--------------------------------------------------");
}

static void dumpMMUContextv2(uint8_t context)
{
  uint8_t page;

  if (context >= MAX_MEMORY_CONTEXTS)
    return;

  Serial1.print("CTX ");
  Serial1.print(context);
  Serial1.print(" : ");

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

void dumpMMUPageMapsCompact()
{
  MemoryModel* model;
  uint8_t ctx;

  model = findModel(memoryConfig.active_model);
  if (!model) {
    Serial1.println("*E: Active model not found");
    return;
  }

  Serial1.println("--------------------------------------------------");
  Serial1.println("MMU PAGE MAPS (COMPACT)");
  Serial1.println("--------------------------------------------------");

  for (ctx = 0; ctx < model->contexts; ctx++)
    dumpMMUContextv2(ctx);

  Serial1.println("--------------------------------------------------");
}