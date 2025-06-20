// 
// 
// 

#include "control.h"
#include "mmu.h"
#include "bus.h"

#define MMU_VALIDATE  1

uint8_t gDefaultMMU[16] = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x8F }; // straight 64k space with IO page on: F000 - FFFF

volatile uint32_t gMMUIOCount = 0L;

/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint32_t getMMUIOCount() {
  return gMMUIOCount;
}

//// <summary>
/// control RW (NEObus)
/// </summary>
/// <param name="vRW"></param>
//inline __attribute__((always_inline))
void setpRW(const uint8_t vRW) {
  gpio_put(pRW, vRW);
}

/// <summary>
/// control pMMUARegHLatch
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void setMMUARegHLatch(const uint8_t vHL) {
    gpio_put(pMMUARegHLatch, vHL);
}

/// <summary>
/// cotrol pMMUDRegOE
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void setMMUDRegOE(const uint8_t vHL) {
    gpio_put(pMMUDRegOE, vHL);
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
/// basic interrupt routine on IO pin FALLING
/// 
/// valid after init of MMU
/// </summary>
static void intMMUIO() {
//  setDebug(mLOW);

  gMMUIOCount++;

//  setDebug(mHIGH);
}

/// <summary>
/// 
/// </summary>
void setupMMU()
{
  gpio_init(pRW);               // Always init pins first
  gpio_set_dir(pRW, GPIO_OUT);   // Set as output
  setpRW(mHIGH);

  gpio_init(pMMUARegHLatch);               // Always init pins first
  gpio_set_dir(pMMUARegHLatch, GPIO_OUT);   // Set as output
  setMMUARegHLatch(mHIGH);          // no latch

  gpio_init(pMMUDRegOE);               // Always init pins first
  gpio_set_dir(pMMUDRegOE, GPIO_OUT);   // Set as output
  setMMUDRegOE(mHIGH);              // no output

  gpio_init(pMMUIO);               // Always init pins first
  gpio_set_dir(pMMUIO, GPIO_IN);   // Set as output
  gpio_pull_up(pMMUIO);            // Enable pull-up resistor
}

//
static uint8_t gContext = 0x00;

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
    Serial.printf("*E: readMMUIndex: wrong mode\n");

  return 0;
}

/// <summary>
/// latch address register A12..15
/// </summary>
/// <param name="vIndex"></param>
//inline __attribute__((always_inline))
void writeMMUIndex(const uint8_t vIndex) {
  if (getControlMode() == mRPI) {
    writeCPUAddressH(vIndex << 4);
  }
  else
    Serial.printf("*E: writeMMUIndex: wrong control mode\n");
}
/// <summary>
/// latch context register 00..255
/// </summary>
/// <param name="vContext"></param>
//inline __attribute__((always_inline))
void writeMMUContext(const uint8_t vContext) {
  writeDataBus(vContext); // write context

  setMMUARegHLatch(mLOW); // arm latching

  DELAY_FACTOR_SHORT();

  setMMUARegHLatch(mHIGH); // latch

  resetDataBus(); // databus to read

  gContext = vContext;
}

/// <summary>
/// 
/// </summary>
/// <param name="vContext"></param>
/// <param name="vIndex"></param>
/// <returns></returns>
uint8_t readMMUPage(const uint8_t vContext, const uint8_t vIndex) {
  if (getControlMode() == mRPI) {
    writeMMUContext(vContext);    // set context 00.255
    writeMMUIndex(vIndex);        // set address 00.15

    setCPUARegOE(mLOW);         // enable address output 4 index

    setpRW(mREAD);              // read action (to be sure)

    setMMUDRegOE(mLOW);         // enable DBuffer

    DELAY_FACTOR_SHORT();
    DELAY_FACTOR_SHORT();
    DELAY_FACTOR_SHORT();
    DELAY_FACTOR_SHORT();

    uint8_t lPage = readDataBus();

    setMMUDRegOE(mHIGH);        // disable DBuffer

    setCPUARegOE(mHIGH);        // disable output address

    resetDataBus();             //

//    Serial.printf("*D: readMMUPage %02X %02X : %02X\n", vContext, vIndex, lPage);

    return lPage;

  }
  else
    Serial.printf("*E: readMMUPage: wrong mode\n");

  return 0;
}

/// <summary>
/// write MMU
/// </summary>
/// <param name="vContext"></param>
/// <param name="vIndex"></param>
/// <param name="vPage"></param>
/// <returns></returns>
bool writeMMUPage(const uint8_t vContext, const uint8_t vIndex, const uint8_t vPage) {
  writeMMUContext(vContext);    // set context 00.255
  writeMMUIndex(vIndex);        // set address 00.15

  writeDataBus(vPage);        // data on NeoDBus

  setCPUARegOE(mLOW);         // enable output address

  setMMUDRegOE(mLOW);         // enable DBuffer

  setpRW(mWRITE);             // write action

  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();

  setpRW(mREAD);              // read (commit write)

  DELAY_FACTOR_SHORT();

  setMMUDRegOE(mHIGH);        // disable DBuffer

  setCPUARegOE(mHIGH)   ;     // disable output address

  resetDataBus();             //

#if MMU_VALIDATE
//  Serial.printf("*D: writeMMUPage: %02X %02X => %02X\n", vContext, vIndex, vPage);

  // validate
  uint8_t lData = readMMUPage(vContext, vIndex);

  if (lData != vPage)
    Serial.printf("*E: writeMMUPage: @%02X %02X %02X (%02X)\n", vContext, vIndex & 0x0F, vPage, lData);

  return (lData == vPage);
#else
  return (true);
#endif
}

/// <summary>
/// dump the Pages of Context
/// </summary>
/// <param name="vContext"></param>
void dumpMMUContext(const uint8_t vContext) {
  Serial.printf("C %02X:", vContext);

  for (uint8_t lPage = 0; lPage < MUM_CONTEXT_PAGES; lPage++) {
    Serial.printf(" %02X", readMMUPage(vContext, lPage));
  }
  Serial.printf("\n");
}

/// <summary>
/// set a MMU context
/// </summary>
/// <param name="vContext"></param>
/// <param name="vMMU"></param>
/// <returns>bool</returns>
bool defMMUContext(const uint8_t vContext, const uint8_t *vMMU) {
    uint16_t lErrCount = 0;

    for (uint8_t lPage = 0; lPage < MUM_CONTEXT_PAGES; lPage++) {
      if (!writeMMUPage(vContext, lPage, vMMU[lPage])) {
        lErrCount++;
      }
//      else
//        Serial.printf("*D: defMMUContext: %02X %02X: %02X\n", vContext, lPage, readMMUPage(vContext, lPage));
    }

//    dumpMMUContext(vContext);

    return (lErrCount == 0);
}

/// <summary>
/// diasble mmuIO interrupt
/// </summary>
void disableMMUInterrupt() {
  detachInterrupt(pMMUIO);
}

/// <summary>
/// enable mmuIO interrupt
/// </summary>
void enableMMUInterrrupt() {
  attachInterrupt(pMMUIO, intMMUIO, FALLING); // interrupt on IO page
}

/// <summary>
/// fill MMU with 256 contexts of straight 64k RAM space
/// </summary>
/// <returns>bool</returns>
bool initMMU() {
    uint16_t lContext;
    uint16_t lErrCount = 0L;

    disableMMUInterrupt();

    // for all contexts
    for (lContext = 0; lContext < NUM_CONTEXTS; lContext++) {
        if (! defMMUContext((uint8_t)lContext, gDefaultMMU))
            lErrCount++;
    }

    // default context
    writeMMUContext(0x00);

    if (lErrCount == 0) {
      enableMMUInterrrupt(); // interrupt on IO page
    }
    else
      Serial.printf("*E: initMMU failure\n");

    return (lErrCount == 0);
}

#include "bus.h"

/// <summary>
/// 
/// </summary>
void testMMU() {
  uint16_t lad = random(0x0000, 0xFFFF);
  uint8_t ld = random(0, 0xFF);

  setDebug(mLOW);

  write6502Memory(lad, ld);
  
  uint8_t t = read6502Memory(lad);

  setDebug(mHIGH);

  if (ld != t) {
    Serial.printf("*E: mem error @%04X %02X : %02X\n", lad, ld, t);
  }
}
