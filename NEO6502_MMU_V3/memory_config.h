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
#include <LittleFS.h>
#include <stdint.h>

#define MAX_MEMORY_NAME        16
#define MAX_MEMORY_MODELS      4
#define MAX_MEMORY_LAYOUTS     8
#define MAX_MEMORY_REGIONS     16
#define MAX_MEMORY_CONTEXTS    8

enum MemoryRegionType {
  MEMORY_REGION_NORMAL = 0,
  MEMORY_REGION_IO
};

enum MemoryIniErrorCode {
  MEMORY_INI_OK = 0,
  MEMORY_INI_OPEN_FAILED,
  MEMORY_INI_SYNTAX,
  MEMORY_INI_UNKNOWN_SECTION,
  MEMORY_INI_UNKNOWN_KEY,
  MEMORY_INI_DUPLICATE_MODEL,
  MEMORY_INI_DUPLICATE_LAYOUT,
  MEMORY_INI_DUPLICATE_REGION,
  MEMORY_INI_DUPLICATE_CONTEXT,
  MEMORY_INI_INVALID_CONTEXT,
  MEMORY_INI_INVALID_VALUE,
  MEMORY_INI_INVALID_RANGE,
  MEMORY_INI_MODEL_NOT_FOUND,
  MEMORY_INI_LAYOUT_NOT_FOUND,
  MEMORY_INI_LAYOUT_INCOMPLETE,
  MEMORY_INI_OVERLAPPING_REGIONS
};

struct MemoryIniError {
  MemoryIniErrorCode code;
  int line;
  char section[48];
  char key[32];
  char value[64];
};

struct MemoryModel {
  char name[MAX_MEMORY_NAME + 1];
  uint8_t contexts;
};

struct MemoryLayoutRegion {
  char name[MAX_MEMORY_NAME + 1];
  uint8_t start;
  uint8_t pages;
  bool shared;
  MemoryRegionType type;
  bool trap_write;
};

struct MemoryLayout {
  char model[MAX_MEMORY_NAME + 1];
  char name[MAX_MEMORY_NAME + 1];
  uint8_t region_count;
  MemoryLayoutRegion regions[MAX_MEMORY_REGIONS];
};

struct MemoryContextBinding {
  char model[MAX_MEMORY_NAME + 1];
  uint8_t context;
  char layout[MAX_MEMORY_NAME + 1];
};

struct MemoryConfig {
  uint8_t version;
  char active_model[MAX_MEMORY_NAME + 1];
  uint8_t boot_context;

  uint8_t model_count;
  MemoryModel models[MAX_MEMORY_MODELS];

  uint8_t layout_count;
  MemoryLayout layouts[MAX_MEMORY_LAYOUTS];

  uint8_t context_count;
  MemoryContextBinding contexts[MAX_MEMORY_CONTEXTS];
};

extern MemoryConfig memoryConfig;
extern MemoryIniError lastMemoryIniError;

bool initializeMemoryConfig();
bool parseMemoryIni(File& file);
bool configureMMUFromActiveModel();

void dumpMemoryConfig();
void dumpMMUPhysicalUsage();
void dumpMMUPageMap(uint8_t context);
void dumpMMUPageMapsCompact();

const char* memoryIniErrorToString(MemoryIniErrorCode code);
