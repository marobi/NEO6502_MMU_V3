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

#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "control.h"
#include "mmu.h"
#include "neobus.h"

static int gCurrentContext = -1;
static int gCurrentIndex = -1;

static uint8_t gDefaultMMU[16] = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x8D,0x0E,0x0F }; // straight 64k space with IO page on: D000 - DFFF

volatile uint32_t gMMUIOCount = 0L;
volatile bool     gMMUIOTrigger = false;

/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint32_t getMMUIOCount() {
  return gMMUIOCount;
}

//-----------------------------------------------------------------------------------------------

/// <summary>
/// get IO page status
/// </summary>
/// <returns></returns>
bool getMMUIO() {
  return (!gpio_get(pMMUIO));
}


/// <summary>
/// intrMMUIO: interrupt handler for MMU IO pin, triggered on falling edge (active low)
/// </summary>
static void intrMMUIO() {
  gpio_acknowledge_irq(pMMUIO, GPIO_IRQ_EDGE_FALL);

  gMMUIOCount++;
  gMMUIOTrigger = true;
}

/// <summary>
/// 
/// </summary>
void setupMMU()
{
  gpio_init(pRW);               // Always init pins first
  gpio_set_dir(pRW, GPIO_OUT);   // Set as output
  PRWPin::high();                // default to read mode

  gpio_init(pMMUARegHLatch);               // Always init pins first
  gpio_set_dir(pMMUARegHLatch, GPIO_OUT);   // Set as output
  MMUARegHLatchPin::high();     // no latch

  gpio_init(pMMUDRegOE);               // Always init pins first
  gpio_set_dir(pMMUDRegOE, GPIO_OUT);   // Set as output
  MMUDRegOEPin::high();            // no output

  gpio_init(pMMUIO);               // Always init pins first
  gpio_set_dir(pMMUIO, GPIO_IN);   // Set as output
  gpio_pull_up(pMMUIO);            // Enable pull-up resistor
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint8_t readMMUIndex() {
  if (getControlMode() == mRPI) {
    uint8_t lindex = (readCPUAddress() >> 12) & 0x0F;

    return lindex;
  }
  else
    Serial1.printf("*E: readMMUIndex: wrong mode\n");

  return 0;
}

/// <summary>
/// latch address register A12..15
/// </summary>
/// <param name="vIndex"></param>
void writeMMUIndex(const uint8_t vIndex) {
  if (vIndex != gCurrentIndex) {
    //  Serial1.printf("*D: writeMMUIndex: %02X\n", vIndex);

    writeCPUAddressH((vIndex & 0x0F) << 4);
    gCurrentIndex = vIndex;
  }
}


/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint8_t getMMUContext() {
  return gCurrentContext;
}

/// <summary>
/// latch context register 00..127
/// </summary>
/// <param name="vContext"></param>
void setMMUContext(const uint8_t vContext) {
  if (vContext != gCurrentContext) {
//    Serial1.printf("*D: writeMMUContext: %02X --> %02X\n", gCurrentContext, vContext);

    writeNEOBus(vContext); // write context

    MMUARegHLatchPin::low(); // arm latching

    delayNs<70>();

    MMUARegHLatchPin::high(); // latch

    resetNEOBus(); // databus to read

    gCurrentContext = vContext;
  }
}

/// <summary>
/// 
/// </summary>
/// <param name="vContext"></param>
/// <param name="vIndex"></param>
/// <returns></returns>
uint8_t readMMUPage(const uint8_t vContext, const uint8_t vIndex) {

  setMMUContext(vContext);     // set context 00.127
  writeMMUIndex(vIndex);       // set address 00.15

  CPUARegOEPin::low();         // enable output address

  PRWPin::high();              // read action (to be sure)

  MMUDRegOEPin::low();         // enable MMU DBuffer

  delayNs<70>();

  uint8_t lPage = readNEOBus();

  MMUDRegOEPin::high();       // disable DBuffer

  CPUARegOEPin::high();       // disable output address
  
  //Serial1.printf("*D: readMMUPage %02X %02X : %02X\n", vContext, vIndex, lPage);
  return lPage;
}

/// <summary>
/// write MMU Page Index (00..15) of Context (00..127) with Page value (00..FF)
/// </summary>
/// <param name="vContext"></param>
/// <param name="vIndex"></param>
/// <param name="vPage"></param>
/// <returns></returns>
bool writeMMUPage(const uint8_t vContext, const uint8_t vIndex, const uint8_t vPage) {
  setMMUContext(vContext);   // set context 00.127
  writeMMUIndex(vIndex);       // set address 00.15

  CPUARegOEPin::low();         // enable output address

  writeNEOBus(vPage);          // data on NeoDBus

  MMUDRegOEPin::low();         // enable DBuffer

  PRWPin::low();               // write action

  delayNs<50>();

  PRWPin::high();             // read (commit write)

  delayNs<20>();

  MMUDRegOEPin::high();       // disable DBuffer

  CPUARegOEPin::high();       // disable output address

  resetNEOBus();

#if USE_VALIDATION
  // validate
  uint8_t lData = readMMUPage(vContext, vIndex);

  if (lData != vPage)
    Serial1.printf("*E: writeMMUPage: @%02X %02X (%02X <> %02X)\n", vContext, vIndex & 0x0F, vPage, lData);

  return (lData == vPage);
#else
  return true;
#endif
}

/// <summary>
/// dump the Pages of Context
/// </summary>
/// <param name="vContext"></param>
void dumpMMUContextCompact(const uint8_t vContext) {
  Serial1.printf("C %02X:", vContext);

  for (uint8_t lPage = 0; lPage < NUM_CONTEXT_PAGES; lPage++) {
    Serial1.printf(" %02X", readMMUPage(vContext, lPage));
  }
  Serial1.printf("\n");
}

/// <summary>
/// set a MMU context
/// </summary>
/// <param name="vContext"></param>
/// <param name="vMMU"></param>
/// <returns>bool</returns>
bool defMMUContext(const uint8_t vContext, const uint8_t* vMMU) {
  uint16_t lErrCount = 0;

  for (uint8_t lPage = 0; lPage < NUM_CONTEXT_PAGES; lPage++) {
    if (!writeMMUPage(vContext, lPage, vMMU[lPage])) {
      lErrCount++;
    }
  }

  return (lErrCount == 0);
}

/// <summary>
/// Map a page @index of a context in the default context @ same index
/// </summary>
/// <param name="vContext"></param>
/// <param name="vPage"></param>
void mapMMUPage(const uint8_t vContext, const uint8_t vIndex) {

  uint8_t lPage = readMMUPage(vContext, vIndex);
  writeMMUPage(DEFAULT_CONTEXT, vIndex, lPage);
}

/// <summary>
/// initialise MMU interrupts
/// </summary>
static void initMMUInterrupt() {
  irq_set_exclusive_handler(IO_IRQ_BANK0, intrMMUIO);
}

/// <summary>
/// enable MMU interrupts on MMU_IO pin FALLING
/// </summary>
void enableMMUInterrupt() {
  gpio_acknowledge_irq(pMMUIO, GPIO_IRQ_EDGE_FALL);
  irq_set_enabled(IO_IRQ_BANK0, true);
  gpio_set_irq_enabled(pMMUIO, GPIO_IRQ_EDGE_FALL, true);
}

/// <summary>
/// disable MMU interrupts on MMU_IO pin FALLING
/// </summary>
void disableMMUInterrupt() {
  gpio_set_irq_enabled(pMMUIO, GPIO_IRQ_EDGE_FALL, false);
  gpio_acknowledge_irq(pMMUIO, GPIO_IRQ_EDGE_FALL);
  irq_set_enabled(IO_IRQ_BANK0, false);
}
/// <summary>
/// fill MMU with 256 contexts of straight 64k RAM space
/// </summary>
/// <returns>bool</returns>
bool initMMU() {
  uint16_t lContext;
  uint16_t lErrCount = 0L;

  initMMUInterrupt();
  disableMMUInterrupt();

  // for all contexts set the pages
  for (lContext = 0; lContext < NUM_CONTEXTS; lContext++) {
    if (!defMMUContext((uint8_t)lContext, gDefaultMMU))
      lErrCount++;
  }

  // default context
  setMMUContext(DEFAULT_CONTEXT);

  if (lErrCount == 0) {
    enableMMUInterrupt(); // interrupt on IO page
  }
  else
    Serial1.printf("*E: initMMU failure\n");

  return (lErrCount == 0);
}

#if 0
/// <summary>
/// 
/// </summary>
void testMMU() {
  Serial1.printf("*D: Testing MMU\n");

  while (true) {
    writeMMUPage(random(NUM_CONTEXTS), random(NUM_CONTEXT_PAGES), random(256));
  }
}
#endif
