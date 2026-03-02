#pragma once

#include <Arduino.h>
#include <stdint.h>

// =====================================================
// LIMITS
// =====================================================

#define MAX_MEMORY_MODELS    8
#define MAX_MEMORY_REGIONS   8
#define MAX_MEMORY_LINE      160
#define MAX_MEMORY_NAME      32

// =====================================================
// ERROR CODES (aligned with sys_config.cpp style)
// =====================================================

enum MemoryIniErrorCode {
  MEMORY_INI_OK = 0,
  MEMORY_INI_ERR_LINE_TOO_LONG,
  MEMORY_INI_ERR_SYNTAX,
  MEMORY_INI_ERR_INVALID_IDENTIFIER,
  MEMORY_INI_ERR_DUPLICATE_SECTION,
  MEMORY_INI_ERR_LIMIT_EXCEEDED,
  MEMORY_INI_ERR_UNKNOWN_MODEL,
  MEMORY_INI_ERR_VERSION_MISMATCH,
  MEMORY_INI_ERR_VALIDATION_FAILED
};

// =====================================================
// MEMORY DATA STRUCTURES
// =====================================================

enum MemoryRegionType {
  MEMORY_REGION_NORMAL = 0,
  MEMORY_REGION_IO = 1
};

struct MemoryRegion {
  char name[MAX_MEMORY_NAME];
  uint8_t start_position;
  uint8_t pages;
  bool shared;
  MemoryRegionType type;
  bool trap_write;
};

struct MemoryModel {
  char name[MAX_MEMORY_NAME];
  uint8_t contexts;
  uint8_t region_count;
  MemoryRegion regions[MAX_MEMORY_REGIONS];
};

struct MemoryConfig {
  uint8_t version;
  char active_model[MAX_MEMORY_NAME];
  uint8_t model_count;
  MemoryModel models[MAX_MEMORY_MODELS];
};

// =====================================================
// GLOBAL CONFIG TABLE
// =====================================================

extern MemoryConfig memoryConfig;

// =====================================================
// PUBLIC API
// =====================================================

bool parseMemoryIni(File& file);
bool initializeMemoryConfig();
bool configureMMUFromActiveModel();
void dumpMemoryConfig();
void dumpMMUPhysicalUsage();
void dumpMMUPageMap(const uint8_t context);
void dumpMMUPageMapsCompact();

const char* memoryIniErrorToString(MemoryIniErrorCode code);
