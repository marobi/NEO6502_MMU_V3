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
#include "p6502.h"
#include "ram.h"
#include "monitor.h"
#include "bios.h"
#include "cmd_proc.h"
#include "cmd.h"
#include "vdu.h"
#include "vdu_graphics.h"
#include "vdu_interface.h"

#include "rom.h"
#include "rom_monitor.h"
#include "rom_bios.h"
#include "rom_test.h"

/// <summary>
/// 
/// </summary>
/// <param name="vFile"></param>
void printFile(const char* vFile) {
  Serial.println("----------");

  File f = LittleFS.open(vFile, "r");
  if (f.available()) {
    Serial.printf("File %s exists\n", vFile);
    while (f.available()) {
      Serial.write(f.read());
    }
    f.close();
  }
  else {
    Serial.printf("File %s does not exist\n", vFile);
  }

  Serial.println("----------");
}

/// <summary>
/// 
/// </summary>
void introDisplay() {
  setTColor(69);  // text color light blue
  vduPrintf("\n\nVersion v%s\n\n", VERSION);

  vduSetReg(R6, DEFAULT_MODE);  vduSetCmd(CMD_VDU);    // VDU mode

  setTColor(9); // text color red

  vduSetReg(R7, FUCHSIA); vduSetCmd(CMD_GCOLOR);
  vduSetCmdx(CMD_CIRC, 3, 100, 100, 100);

  vduSetReg(R7, AQUA); vduSetCmd(CMD_GCOLOR);
  vduSetReg(R6, 1);  vduSetCmd(CMD_MODE);  // fill mode
  vduSetCmdx(CMD_RECTR, 5, 100, 100, 100, 40, 8);

  vduSetReg(R7, BLACK); vduSetCmd(CMD_GCOLOR);
  vduSetReg(R6, 0); vduSetCmd(CMD_MODE);   // non-fill mode
  vduSetCmdx(CMD_RECTR, 5, 100, 100, 100, 40, 8);
  vduSetCmdx(CMD_RECTR, 5, 102, 102, 96, 36, 8);
  vduSetCmdx(CMD_RECTR, 5, 104, 104, 92, 32, 8);
  vduSetCmdx(CMD_RECTR, 5, 106, 106, 88, 28, 8);
  vduSetReg(R6, 1);  vduSetCmd(CMD_MODE);  // fill mode
  vduSetReg(R7, YELLOW); vduSetCmd(CMD_GCOLOR);
  vduSetCmdx(CMD_RECTR, 5, 108, 108, 84, 24, 8);

  vduSetReg(R6, 0); vduSetCmd(CMD_MODE);   // non-fill mode
  vduSetReg(R7, BLACK); vduSetCmd(CMD_GCOLOR);
  vduSetCmdx(CMD_RECTR, 5, 108, 108, 84, 24, 8);

  vduSetReg(R7, RED); vduSetCmd(CMD_GCOLOR);
  vduSetCmdx(CMD_LINE, 4, 100, 100, WIDTH - 10, HEIGHT - 10);

  vduPrintStr("Happy?\n");

  setTColor(DEFAULT_COLOR);

  dumpVDURegisterSet();
}

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
  initVDUInterface(); // init VDU command interface
  introDisplay();

  initCmdSlots();
  initCmdProcessor();

  printFile("intro.txt");

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

  Serial.println("Test program @");
  loadROM(test_bin);

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
