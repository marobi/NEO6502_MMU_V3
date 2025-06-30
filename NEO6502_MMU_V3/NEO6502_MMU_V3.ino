/*
 Name:		NEO6502_MMU_V3
 Created:	14.6.2025
 Author:	Rien Matthijsse
*/

#include <Arduino.h>
#include "config.h"
#include "control.h"
#include "mmu.h"
#include "bus.h"
#include "vdu.h"
#include "p6502.h"
#include "ram.h"
#include "monitor.h"
#include "disasm6502.h"

// base = 0xfff0
static const uint8_t gBin[16] = {
 0x4c, 0xf0, 0xff,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00,  // irq
 0xf0, 0xff,  // reset 
 0x00, 0x00   // nmi
};


/// <summary>
/// test
/// </summary>
void test() {
//  testMMU();
   testBUS();
}

/// <summary>
/// setup
/// </summary>
void setup() {
  setupControl(); // As early as possible
  setup6502();
  setupCPU();
  setupMMU();

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

  initVDU();      // get display running

  Serial.printf("*I: setup done\n");

  // report clock freqs.
  uint32_t freq = clock_get_hz(clk_sys);
  Serial.printf("*I: Core frequency: %d MHz\n", freq / MHZ);
  Serial.printf("*I: 6502 frequency: %d MHz\n", DEFAULT_6502_CLOCK / MHZ);
  Serial.println();

  Serial.printf("Test program @\n");
  if (loadBinary(0xFFF0, 16, &gBin[0]))      // load some sample binary
    dumpMemory(0xFFF0, 0XFFFF);              // show it
  else
    Serial.printf("*E: Binary not loaded\n");

  initMonitor();

  //// test
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

/// <summary>
/// loop for ever
/// </summary>
void loop() {
  monitor();
  // test
  test();
}
