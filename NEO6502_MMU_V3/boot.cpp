// 
// 
// 
#include "boot.h"
#include "sys_config.h"
#include "mmu.h"
#include "rom.h"

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

  Serial.println();
  Serial.println("==== SYSTEM CONFIGURATION ====");

  for (uint8_t i = 0; i < visibleCount; i++) {

    int cfgIndex = visibleConfigs[i];

    Serial.printf("[%d] %s", i, configs[cfgIndex].name);

    if (cfgIndex == systemConfig.defaultConfig)
      Serial.print("  (default)");

    Serial.println();
  }

  Serial.println();
  Serial.println("Enter number and press ENTER.");
  Serial.println("Press ENTER only to boot default.");
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

    if (Serial.available()) {

      String input = Serial.readStringUntil('\n');
      input.trim();

      if (input.length() == 0)
        return systemConfig.defaultConfig;

      int selection = input.toInt();

      if (selection >= 0 && selection < visibleCount)
        return visibleConfigs[selection];

      Serial.println("Invalid selection.");
      showConfigMenu();
    }
  }

  Serial.println("Timeout. Booting default.");
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

  for (uint8_t i = 0; i < configs[cfgIndex].count; i++) {

    uint8_t cartIndex = configs[cfgIndex].entries[i].cartIndex;
    uint8_t ctx = configs[cfgIndex].entries[i].context;

    char fullPath[64];
    snprintf(fullPath, sizeof(fullPath), "/system/%s", cartridges[cartIndex].file);

    Serial.printf("*D: Loading ROM %s into context %d\n", fullPath, ctx);
    
    // read image of ROM file into memory
    data = readBinaryFile(fullPath);
    if (data) {
      // Activate context before loading
      writeMMUContext(ctx);

      // load ROM
      if (!loadROMCartridge(data)) {
        Serial.println("*E: Cartridge load failed.");
        return false;
      }
    }
    else {
      Serial.println("*E: Cartridge load failed.");
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

  Serial.printf("*I: Activating configuration: %s\n",
    configs[cfgIndex].name);

  if (!activateConfiguration(cfgIndex)) {

    Serial.println("*E: Activation failed. Using fallback.");

    loadFallbackProfile();
    activateConfiguration(systemConfig.defaultConfig);
  }
}
