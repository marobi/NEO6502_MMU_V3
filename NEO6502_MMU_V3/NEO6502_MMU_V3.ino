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
#include "tusb_config.h"
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
#include "gdu.h"
#include "cmd_interface.h"

#include "rom.h"

#include "memory_config.h"
#include "sys_config.h"
#include "boot.h"

#include "simple_io.h"

#include "monitor.h"
#include "input.h"
#include "mailbox.h"

#include "scheduler.h"

#include "usb_storage.h"
#include "usb_hid_input.h"

#include "indicator.h"

/// <summary>
/// print the contents of a file from LittleFS to the serial console for debugging purposes.
/// </summary>
/// <param name="vFile"></param>
static void printFile(const char* vFile) {
  Serial1.println("----------");

  File f = LittleFS.open(vFile, "r");
  if (f.available()) {
//    Serial1.printf("File %s exists\n", vFile);
    while (f.available()) {
      vduPutc(f.read());
    }
    f.close();
  }
  else {
    Serial1.printf("*E: printFile: File %s does not exist\n", vFile);
  }

  Serial1.println("----------");
}


/// <summary>
/// setup the system, including initializing the 6502 CPU, MMU, gdu, and other components. 
/// Also mounts the LittleFS filesystem and displays an intro screen on the gdu. 
/// Finally, it boots the system with a menu to select the configuration to boot with.
/// </summary>
void setup() {
  setupControl(); // As early as possible
  setupNEOBus();
  setupMMU();
  setup6502();

  //Serial.begin(115200);
  Serial1.begin(115200);
  delay(2500);

  // init 6502 into BOOT-mode
  init6502();
  Serial1.printf("*I: 6502 init OK\n");

  // init & validate setup of MMU
  if (initMMU()) {
    Serial1.printf("*I: MMU init OK\n");
  }
  else {
    Serial1.println("*E: MMU init FAILED");
    Serial1.flush();
    delay(5000);
  }

//  testMMU();

  if (! LittleFS.begin()) {
    Serial1.println("*E: LittleFS mount failed");
  }
  else
    Serial1.println("*I: LittleFS mount OK");

  initVDU();           // get display running
  initCmdInterface();  // init CMD command interface

  introDisplay();      // show intro

//  printFile("intro.txt");

  Serial1.println("*I: setup done");

  Serial1.printf("*I: BIOS: %s %s\n", BIOS_DATE, BIOS_TIME);
  
  // report clock freqs.
  uint32_t freq = clock_get_hz(clk_sys);
  Serial1.printf("*I: Core frequency: %4d MHz\n", freq / MHZ);
  Serial1.printf("*I: 6502 frequency: %2.1f MHz\n", (float)DEFAULT_6502_CLOCK / MHZ);

  initializeMemoryConfig();     // load /memory.ini
  configureMMUFromActiveModel();  // configure MMU

  dumpMMUPageMapsCompact();     // show MMU config results
  dumpMMUPhysicalUsage();       // dump physical page usage

  initializeSystemConfig();     // init system configuration from /system.ini

//  dumpSystemConfig();

  bootSystemWithMenu();         // load/boot system with menu to select configuration.

  Serial1.printf("\n*D: default context: CTX%1X\n", memoryConfig.boot_context);

  //  fillMemory(0x00);             // clear memory 64k of current context

  initMailbox();

  initCmdInterface();           // init command interface
  initInput();
  initSimpleIO();               // simple I/O between CPU and VDU (active only when console PID is 0)

  initScheduler();              // init context switching

  set6502State(sRESET);         // reset CPU

  initUSBStorage();
  initUSBHIDInput();

  initMonitor();                // init monitor after boot

  initIndicator();              // setup board led indicator

}

/// <summary>
/// loop function runs repeatedly after setup 
/// processing any serial input for the gdu
/// and is responsible for running the gdu task, 
/// and running the monitor.
/// </summary>
void loop() {
  taskUSBStorage();              // USB storage task: TinyUSB MSC + FatFs mount

  taskUSBHIDInput();            // USB HID keyboard/mouse input, staged independently from storage

  taskIRQTimer();

  taskScheduler();

  taskSimpleIO();               // simple I/O between CPU and VDU (active only when console PID is 0)

  taskInput();                  // process FIFOs

  taskMailbox();

  taskVDU();                    // vdu task, mainly control of cursor blinking and smooth scroll

  taskICMonitor();              // ICM

  updateIndicator();            // manage status LED

//  delayNs<500>();
}
