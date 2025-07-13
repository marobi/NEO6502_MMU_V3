/*
 Name:		NEO6502_MMU_V3
 Created:	14.6.2025
 Author:	Rien Matthijsse
*/

#include <Arduino.h>
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
#include "rom_test.h"
#include "rom_bios.h"

/// <summary>
/// setup
/// </summary>
void setup() {
  setupControl(); // As early as possible
  setup6502();
  setupCPU();
  setupMMU();

  Serial.begin(115200);
  delay(1500);

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

  if (! LittleFS.begin()) {
    Serial.println("*E: LittleFS mount failed");
  }
  else
    Serial.println("*I: LittleFS mount OK");

  initVDU();      // get display running

  initCmdProcessor();

  Serial.printf("*I: setup done\n");

  Serial.printf("*I: BIOS: %s %s\n", BIOS_DATE, BIOS_TIME);
  // report clock freqs.
  uint32_t freq = clock_get_hz(clk_sys);
  Serial.printf("*I: Core frequency: %d MHz\n", freq / MHZ);
  Serial.printf("*I: 6502 frequency: %d MHz\n", DEFAULT_6502_CLOCK / MHZ);

  Serial.printf("\nTest program @\n");
  loadROM(test_bin);

  // load bios
  Serial.printf("BIOS program @\n");
  loadROM(bios_bin);

  Serial.println();

  set6502State(sRESET);

  initMonitor();

  // test
  //dumpMemory(0x0FF0, 0X0FFF);

  //Serial.println();

  //// replaced a page
  //writeMMUPage(0x00, 0x0F, 0x9F);
  //dumpMMUContext(0x00);
  //dumpMemory(0xFFF0, 0XFFFF);              // show it

  //Serial.println();

  //// dup pages
  //writeMMUPage(0x00, 0x0F, 0x8F);
  //writeMMUPage(0x00, 0x00, 0x8F);
  //dumpMMUContext(0x00);

  //// show it
  //dumpMemory(0xFFF0, 0xFFFF);
  //dumpMemory(0x0FF0, 0x0FFF);
  //// end test
}

static uint8_t tmp;
/// <summary>
/// loop for ever
/// </summary>
void loop() {
  uint8_t lChar;

  monitor();

//  if (read6502Char(&lChar))
//    Serial.print(lChar);

  snoop_write6502Memory(0XFC10, 1, &tmp);

  tmp++;

  delay(5);
//  testBUS();
}
