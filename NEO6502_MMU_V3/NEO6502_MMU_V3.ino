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
#include "p6502.h"
#include "ram.h"
#include "bios.h"
#include "cmd.h"
#include "vdu.h"
#include "vdu_graphics.h"
#include "vdu_interface.h"

#include "rom.h"

#include "memory_config.h"

#include "sys_config.h"
#include "boot.h"

#include "monitor.h"

/// <summary>
/// print the contents of a file from LittleFS to the serial console for debugging purposes.
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
/// intro display on VDU to show that the system is up and running, and to test some basic VDU graphics capabilities.
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
/// setup the system, including initializing the 6502 CPU, MMU, VDU, and other components. 
/// Also mounts the LittleFS filesystem and displays an intro screen on the VDU. 
/// Finally, it boots the system with a menu to select the configuration to boot with.
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

  printFile("intro.txt");

  Serial.println("*I: setup done");

  Serial.printf("*I: BIOS: %s %s\n", BIOS_DATE, BIOS_TIME);
  
  // report clock freqs.
  uint32_t freq = clock_get_hz(clk_sys);
  Serial.printf("*I: Core frequency: %0d MHz\n", freq / MHZ);
  Serial.printf("*I: 6502 frequency: %0.1f MHz\n", (float)DEFAULT_6502_CLOCK / MHZ);

  initializeMemoryConfig();
  configureMMUFromActiveModel();

  dumpMMUPhysicalUsage();       // dump physical page usage for debug
  dumpMMUPageMapsCompact();

  initializeSystemConfig();     // init system configuration from /system.ini

  bootSystemWithMenu();         // boot system with menu to select configuration.

  writeMMUContext(DEFAULT_CONTEXT); // set default MMU context for booting

  set6502State(sRESET);

  initMonitor();                // init monitor after boot
}

/// <summary>
/// loop function runs repeatedly after setup 
/// processing any serial input for the VDU
/// and is responsible for running the VDU task, 
/// and running the monitor.
/// </summary>
void loop() {
#if 0
  // process serial input for 6502
  if (Serial.available() > 0) {
    if (outCharAvailable6502()) {
      uint8_t c = Serial.read(); // read char from serial
      outChar6502(c);            // write char to 6502, should succeed
 //     vduPutc(c);                // local echo to VDU for testing, remove if not needed
    }
  }
#endif

  uint8_t c = inChar6502();
  if (c != 0x00) {
    vduPutc(c);                  // echo char from 6502 to VDU
  }

  taskVDU();                     // run VDU task to process VDU commands and update display

  taskICMonitor();               // run monitor to update state

  delay(5);
}
