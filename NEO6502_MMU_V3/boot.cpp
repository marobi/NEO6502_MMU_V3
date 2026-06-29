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
#include "boot.h"
#include "sys_config.h"
#include "memory_config.h"
#include "mmu.h"
#include "p6502.h"
#include "rom.h"
#include "vdu.h"

// ----------------------------------------
// Visible config mapping
// ----------------------------------------
static int visibleConfigs[MAX_CONFIG];
static uint8_t visibleCount = 0;

/// <summary>
/// build config list
/// </summary>
static void buildVisibleConfigList() {
  visibleCount = 0;
  for (uint8_t i = 0; i < MAX_CONFIG; i++) {
    if (configs[i].defined) {
      visibleConfigs[visibleCount++] = i;
    }
  }
}

/// <summary>
/// Menu display of configurations with indication of default
/// </summary>
static void showConfigMenu() {

  buildVisibleConfigList();

  Serial1.println();
  Serial1.println("==== SYSTEM CONFIGURATION ====");

  for (uint8_t i = 0; i < visibleCount; i++) {

    int cfgIndex = visibleConfigs[i];

    Serial1.printf("[%d] %s", i, configs[cfgIndex].name);

    if (cfgIndex == systemConfig.defaultConfig)
      Serial1.print("  (default)");

    Serial1.println();
  }

  Serial1.println();
  Serial1.println("Enter number and press ENTER.");
  Serial1.println("Press ENTER only to boot default.");
}

/// <summary>
/// wait for user to select a configuration with timeout. 
/// If timeout expires, default is returned. 
/// If user presses ENTER without input, default is returned. 
/// If user enters invalid selection, menu is shown again and timeout restarts.
/// </summary>
/// <param name="timeoutMs"></param>
/// <returns></returns>
static int waitForUserSelection(unsigned long timeoutMs) {

  unsigned long start = millis();

  while (millis() - start < timeoutMs) {

    if (Serial1.available()) {

      String input = Serial1.readStringUntil('\n');
      input.trim();

      if (input.length() == 0)
        return systemConfig.defaultConfig;

      int selection = input.toInt();

      if (selection >= 0 && selection < visibleCount)
        return visibleConfigs[selection];

      Serial1.println("*E: Invalid selection.");
      showConfigMenu();
    }
  }

  Serial1.println("*I: Timeout. Booting default.");
  return systemConfig.defaultConfig;
}

/// <summary>
/// activate a configuration by loading its cartridges into their contexts.
/// </summary>
/// <param name="cfgIndex"></param>
/// <returns></returns>
bool activateConfiguration(int cfgIndex) {
  uint8_t* data;

  if (cfgIndex < 0 || cfgIndex >= MAX_CONFIG)
    return false;

  if (!configs[cfgIndex].defined)
    return false;

  set6502State(sBOOT);


  vduPrintStr(" ROM                            CTX Load Size\n");

  for (uint8_t i = 0; i < configs[cfgIndex].count; i++) {

    uint8_t cartIndex = configs[cfgIndex].entries[i].cartIndex;
    uint8_t ctx = configs[cfgIndex].entries[i].context;

    char fullPath[64];
    snprintf(fullPath, sizeof(fullPath), "/system/%s", cartridges[cartIndex].file);

    vduPrintf("%32s %2d ", fullPath, ctx);
    
    // read image of ROM file into memory
    data = readBinaryFile(fullPath);
    if (data) {
      // Activate context before loading
      setMMUContext(ctx);

      // load ROM
      if (!loadROMCartridge(data)) {
        Serial1.println("*E: Cartridge load failed.");
        return false;
      }
    }
    else {
      Serial1.println("*E: Cartridge load failed.");
      return false;
    }
  }

  return true;
}


/// <summary>
/// boot system with menu to select configuration. 
/// If no selection is made within timeout, default configuration is booted. 
/// If selected configuration fails to load, fallback profile is loaded 
/// and default configuration is booted.
/// </summary>
void bootSystemWithMenu() {

  showConfigMenu();

  int cfgIndex = waitForUserSelection(5000);

  Serial1.printf("*I: Loading configuration: %s\n", configs[cfgIndex].name);
  vduPrintf("Loading configuration: %s\n", configs[cfgIndex].name);

  if (! activateConfiguration(cfgIndex)) {

    Serial1.println("*E: Activation failed. Using fallback.");

    loadFallbackProfile();
    activateConfiguration(systemConfig.defaultConfig);
  }

  setMMUContext(memoryConfig.boot_context); // set default MMU context for booting
}
