// 
// 
// 
#include "control.h"
#include "bus.h"
#include "mmu.h"

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

  uint16_t laddress = ((uint16_t)readDataBus() << 8); // read it

  setCPUABufHOE(mHIGH);   // disable hight byte
  setCPUABufLOE(mLOW);    // enable low byte

  laddress |= readDataBus();  // read it

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
  writeDataBus(vAddress);   // write hight byte of address on NEObus
  setCPUARegHLatch(mLOW);   // arm latch

  DELAY_FACTOR_SHORT();     // settle
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();

  setCPUARegHLatch(mHIGH); // latch in AREG
  resetDataBus();          // reset NEObus
}

/// <summary>
/// write Address A00..07
/// </summary>
/// <param name="vAddress"></param>
inline __attribute__((always_inline))
void writeCPUAddressL(const uint8_t vAddress) {
  // set HighAddress A8..15
  writeDataBus(vAddress);   // write hight byte of address on NEObus
  setCPUARegLLatch(mLOW);   // arm latch

  DELAY_FACTOR_SHORT();     // settle
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();

  setCPUARegLLatch(mHIGH); // latch in AREG
  resetDataBus();         // reset NEObus
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
inline __attribute__((always_inline))
uint8_t read6502Data() {
  // read databus
  setmRW(mREAD);        // to be sure
  setCPUDBufOE(mLOW);   // read from databus

  DELAY_FACTOR_SHORT(); // settle
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();

  uint8_t ldata = readDataBus();  // read data

  setCPUDBufOE(mHIGH);  // disable data output

  return ldata;
}

/// <summary>
/// write cycle on 6502 databus
/// </summary>
/// <param name="vData"></param>
inline __attribute__((always_inline))
void write6502Data(const uint8_t vData) {
  writeDataBus(vData);  // write data to Neodbus
  
  setmRW(mWRITE);       // write cycle
  
  setCPUDBufOE(mLOW);   // enable databus

  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();
  DELAY_FACTOR_SHORT();

  setmRW(mREAD);        // end write cycle

  DELAY_FACTOR_SHORT();

  setCPUDBufOE(mHIGH);  // disable databus
  
  resetDataBus();       // disable neodbus
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
      Serial.printf("*E: write6502Memory: 0x04X: 0x%02X (0x%02X)", vAddress, vData, ldata);
    }
  }
  else
    Serial.println("*E: write6502Meory: wrong mode");
}

//
static uint16_t gAddress = 0x0F00;

static uint8_t gDummy[16] = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };

#define DEBUG 1

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary>
/// 
/// </summary>
void testBUS() {
  uint8_t ldata;
  //  delay(10);

#ifdef DEBUG
  setDebug(mLOW);
#endif

  initMMU();
  ldata = read6502Memory(0XFF00);
  Serial.printf("*D: IRD: %04X %02X (%d)\n", gAddress, ldata, getMMUIOCount());
  ldata = read6502Memory(0XFF01);
  Serial.printf("*D: IRD: %04X %02X (%d)\n", gAddress, ldata, getMMUIOCount());

  defMMUContext(0x00, gDummy);

  ldata = read6502Memory(0XFF00);
  Serial.printf("*D: ORD: %04X %02X (%d)\n\n", gAddress, ldata, getMMUIOCount());

  
#ifdef DEBUG
  setDebug(mHIGH);
#endif

}
