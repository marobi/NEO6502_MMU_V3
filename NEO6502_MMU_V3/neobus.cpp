// 
// 
// 
#include "control.h"
#include "neobus.h"
#include "mmu.h"
#include "p6502.h"

/// <summary>
/// control cpuARegLLatch
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void setCPUARegLLatch(const uint8_t vHL) {
  gpio_put(pCPUARegLLatch, vHL);
}

/// <summary>
/// control cpuARegHLatch
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void setCPUARegHLatch(const uint8_t vHL) {
  gpio_put(pCPUARegHLatch, vHL);
}

/// <summary>
/// control cpuARegOE
/// </summary>
/// <param name="vHL"></param>
//inline __attribute__((always_inline))
void setCPUARegOE(const uint8_t vHL) {
  gpio_put(pCPUARegOE, vHL);
}

/// <summary>
/// control pCPUDBufOE
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void setCPUDBufOE(const uint8_t vHL) {
  gpio_put(pCPUDBufOE, vHL);
}

/// <summary>
/// 
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void setCPUABufLOE(const uint8_t vHL) {
  gpio_put(pCPUABufLOE, vHL);
}

/// <summary>
/// 
/// </summary>
/// <param name="vHL"></param>
inline __attribute__((always_inline))
void setCPUABufHOE(const uint8_t vHL) {
  gpio_put(pCPUABufHOE, vHL);
}


/// <summary>
/// setup CPU interface
/// </summary>
void setupCPU() {
  gpio_init(pCPUARegLLatch);                // Always init pins first
  gpio_set_dir(pCPUARegLLatch, GPIO_OUT);   // Set as output
  setCPUARegLLatch(mHIGH);  // no latch

  gpio_init(pCPUARegHLatch);                // Always init pins first
  gpio_set_dir(pCPUARegHLatch, GPIO_OUT);   // Set as output
  setCPUARegHLatch(mHIGH);  // no latch

  gpio_init(pCPUABufLOE);                   // Always init pins first
  gpio_set_dir(pCPUABufLOE, GPIO_OUT);      // Set as output
  setCPUABufLOE(mHIGH);// no latch

  gpio_init(pCPUABufHOE);                   // Always init pins first
  gpio_set_dir(pCPUABufHOE, GPIO_OUT);      // Set as output
  setCPUABufHOE(mHIGH);  // no latch

  gpio_init(pCPUARegOE);                    // Always init pins first
  gpio_set_dir(pCPUARegOE, GPIO_OUT);       // Set as output
  setCPUARegOE(mHIGH);  // no latch

  gpio_init(pCPUDBufOE);                    // Always init pins first
  gpio_set_dir(pCPUDBufOE, GPIO_OUT);       // Set as output
  setCPUDBufOE(mHIGH);  // no latch

}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint16_t readCPUAddress() {
  if (getControlMode() == mRPI) {
    setCPUARegOE(mLOW);       // output AREG address
  }

  setCPUABufHOE(mLOW);        // enable high byte

  uint16_t laddress = ((uint16_t)readNEOBus() << 8); // read it

  setCPUABufHOE(mHIGH);   // disable hight byte
  setCPUABufLOE(mLOW);    // enable low byte

  laddress |= readNEOBus();  // read it

  setCPUABufLOE(mHIGH);   // disable low byte

  if (getControlMode() == mRPI) {
    setCPUARegOE(mHIGH);  // disable AREG output
  }

  return laddress;
}

/// <summary>
/// write Address A08..15
/// </summary>
/// <param name="vAddress"></param>
void writeCPUAddressH(const uint8_t vAddress) {
  // set HighAddress A8..15
  writeNEOBus(vAddress);   // write hight byte of address on NEObus
  setCPUARegHLatch(mLOW);   // arm latch

  DELAY_FACTOR_SHORT();     // settle
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();

  setCPUARegHLatch(mHIGH); // latch in AREG
  resetNEOBus();          // reset NEObus
}

/// <summary>
/// write Address A00..07
/// </summary>
/// <param name="vAddress"></param>
inline __attribute__((always_inline))
void writeCPUAddressL(const uint8_t vAddress) {
  // set HighAddress A8..15
  writeNEOBus(vAddress);   // write hight byte of address on NEObus
  setCPUARegLLatch(mLOW);   // arm latch

  DELAY_FACTOR_SHORT();     // settle
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();

  setCPUARegLLatch(mHIGH); // latch in AREG
  resetNEOBus();         // reset NEObus
}

/// <summary>
/// latch CPU address in AREG
/// </summary>
/// <param name="vAddress"></param>
inline __attribute__((always_inline))
void writeCPUAddress(const uint16_t vAddress) {
  if (getControlMode() == mRPI) {         // only in MMU mode
    writeCPUAddressL(vAddress);           // latch AddressL
    writeCPUAddressH(vAddress >> 8);      // latch AddressH

    uint16_t laddress = readCPUAddress(); // validate
    if (laddress != vAddress) {
      Serial.printf("*E writeCPUAddress: 0x%04X (0x%04X)\n", vAddress, laddress);
    }
  }
  else
    Serial.printf("*E: setCPUAddress: wrong mode\n");
}

/// <summary>
/// read cycle on 6502 databus
/// </summary>
/// <returns></returns>
//inline __attribute__((always_inline))
uint8_t read6502Data() {
  // read databus
  setmRW(mREAD);        // to be sure
  setCPUDBufOE(mLOW);   // read from databus

  DELAY_FACTOR_SHORT(); // settle
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();

  uint8_t ldata = readNEOBus();  // read data

  setCPUDBufOE(mHIGH);  // disable data output

  return ldata;
}

/// <summary>
/// write cycle on 6502 databus
/// </summary>
/// <param name="vData"></param>
inline __attribute__((always_inline))
void write6502Data(const uint8_t vData) {
  writeNEOBus(vData);  // write data to Neodbus
  
  setmRW(mWRITE);       // write cycle
  
  setCPUDBufOE(mLOW);   // enable databus

  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();

  setmRW(mREAD);        // end write cycle

  DELAY_FACTOR_SHORT();

  setCPUDBufOE(mHIGH);  // disable databus
  
  resetNEOBus();       // disable neodbus
}

/// <summary>
/// read CPU memory location
/// </summary>
/// <param name="vAddress"></param>
/// <returns></returns>
uint8_t read6502Memory(const uint16_t vAddress) {
  if (getControlMode() == mRPI) {
    writeCPUAddress(vAddress);

    setCPUARegOE(mLOW);  // output adress on cpu bus

    uint8_t ldata = read6502Data();

    setCPUARegOE(mHIGH); // disable address output

    return ldata;
  }
  else
    Serial.println("*E: write6502Meory: wrong mode");

  return 0;
}

/// <summary>
/// write CPU memory location
/// </summary>
/// <param name="vAddress"></param>
/// <param name="vData"></param>
void write6502Memory(const uint16_t vAddress, const uint8_t vData) {
  if (getControlMode() == mRPI) {
    writeCPUAddress(vAddress);  // latch address

    setCPUARegOE(mLOW);         // output on address bus

    write6502Data(vData);       // write cycle

    uint8_t ldata = read6502Memory(vAddress);  // validate

    setCPUARegOE(mHIGH);       // disable address output

    if (ldata != vData) {
      Serial.printf("*E: write6502Memory: 0x%04X: 0x%02X (0x%02X)\n", vAddress, vData, ldata);
    }
  }
  else
    Serial.println("*E: write6502Meory: wrong mode");
}

/// <summary>
/// Snoop read from memory, halting the possibly running CPU
/// </summary>
/// <param name="vAddress"></param>
/// <param name="vBytes"></param>
/// <param name="vBuffer"></param>
void snoop_read6502Memory(const uint16_t vAddress, const uint16_t vBytes, const uint8_t *vBuffer) {
  bool lCPUHasControl = false;

  if (getControlMode() == mCPU) {
    lCPUHasControl = true;
    // halt, disable CPU
    set6502State(eHALTED, eDISABLED);
  }

  uint16_t lAd = vAddress;
  uint8_t* lBuf = (uint8_t *)vBuffer;
  for (uint16_t m = 0; m < vBytes; m++) {
    lBuf[m] = read6502Memory(lAd++);
  }

  if (lCPUHasControl) {
    // enable, running
    set6502State(eRUN, eENABLED);
  }
}

/// <summary>
/// Snoop write to memory, halting the possible running CPU
/// </summary>
/// <param name="vAddress"></param>
/// <param name="vBytes"></param>
/// <param name="vBuffer"></param>
void snoop_write6502Memory(const uint16_t vAddress, uint16_t vBytes, const uint8_t* vBuffer) {
  bool lCPUHasControl = false;

  if (getControlMode() == mCPU) {
    lCPUHasControl = true;
    set6502State(eHALTED, eDISABLED);
  }

  uint16_t lAd = vAddress;
  for (uint16_t m = 0; m < vBytes; m++) {
    write6502Memory(lAd++, vBuffer[m]);
  }

  if (lCPUHasControl) {
    // enable, running
    set6502State(eRUN, eENABLED);
  }
}

//
static uint16_t gAddress = 0x0F00;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary>
/// 
/// </summary>
void testBUS() {
  uint8_t vData, lData;

  uint16_t lAddress = random(0x0200, 0XEFFF);
  
  vData = random(0xFF);

  setDebug(mLOW);

  snoop_write6502Memory(lAddress, 1, &vData);

  snoop_read6502Memory(lAddress, 1, &lData);

  setDebug(mHIGH);

  if (vData != lData)
    Serial.printf("*E: testBus: error %04X : %02X (%02X)\n", lAddress, vData, lData);
}
