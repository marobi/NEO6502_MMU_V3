/*
 Name:		NEO6502_MMU_V3
 Created:	14.6.2025
 Author:	Rien Matthijsse
*/

#include <arduino.h>
#include <LittleFS.h>

#include "config.h"
#include "control.h"
#include "mmu.h"
#include "neobus.h"
#include "vdu.h"
#include "p6502.h"
#include "ram.h"
#include "monitor.h"
#include "bios.h"
#include "cmd_proc.h"
#include "cmd.h"

#include "rom.h"
#include "rom_monitor.h"
#include "rom_bios.h"
#include "rom_test.h"

/// <summary>
/// setup
/// </summary>
void setup() {
  setupControl(); // As early as possible
  setupMMU();
  setupCPU();
  setup6502();

  Serial.begin(115200);
  delay(2500);

  // turn PHI2 on
  init6502();
  Serial.printf("*I: 6502 init OK\n");

  // init & validate setup of MMU
  if (initMMU()) {
    Serial.printf("*I: MMU init OK\n");
  }
  else {
    Serial.println("*E: MMU init FAILED");
    Serial.flush();
    delay(5000);
  }

//  testMMU();

  if (! LittleFS.begin()) {
    Serial.println("*E: LittleFS mount failed");
  }
  else
    Serial.println("*I: LittleFS mount OK");

    initVDU();      // get display running

  initCmdSlots();
  initCmdProcessor();

  Serial.printf("*I: setup done\n");

  Serial.printf("*I: BIOS: %s %s\n", BIOS_DATE, BIOS_TIME);
  // report clock freqs.
  uint32_t freq = clock_get_hz(clk_sys);
  Serial.printf("*I: Core frequency: %0d MHz\n", freq / MHZ);
  Serial.printf("*I: 6502 frequency: %0.1f MHz\n", (float)DEFAULT_6502_CLOCK / MHZ);

  // load bios
  Serial.println("BIOS program @");
  loadROM(bios_bin);

  Serial.println("Monitor WOZMON @");
  loadROM(wozmon_bin);

#if 0
  Serial.println("Test program @");
  loadROM(test_bin);
#endif

  Serial.println();

  set6502State(sRESET);

  initMonitor();
}

/// <summary>
/// loop for ever
/// </summary>
void loop() {
//  testBUS();

  monitor();

  delay(5);
}
