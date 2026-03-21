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

#include "control.h"
#include "neobus.h"
#include "mmu.h"
#include "p6502.h"

/// <summary>
/// 
/// </summary>
void setupCPU() {
  gpio_init(pCPUARegLLatch);                // Always init pins first
  gpio_set_dir(pCPUARegLLatch, GPIO_OUT);   // Set as output
  CPUARegLLatchPin::high();                 // no latch

  gpio_init(pCPUARegHLatch);                // Always init pins first
  gpio_set_dir(pCPUARegHLatch, GPIO_OUT);   // Set as output
  CPUARegHLatchPin::high();                 // no latch

  gpio_init(pCPUABufLOE);                   // Always init pins first
  gpio_set_dir(pCPUABufLOE, GPIO_OUT);      // Set as output
  CPUABufLOEPin::high();                    // no OE

  gpio_init(pCPUABufHOE);                   // Always init pins first
  gpio_set_dir(pCPUABufHOE, GPIO_OUT);      // Set as output
  CPUABufHOEPin::high();                    // no OE

  gpio_init(pCPUARegOE);                    // Always init pins first
  gpio_set_dir(pCPUARegOE, GPIO_OUT);       // Set as output
  CPUARegOEPin::high();                     // no OE

  gpio_init(pCPUDBufOE);                    // Always init pins first
  gpio_set_dir(pCPUDBufOE, GPIO_OUT);       // Set as output
  CPUDBufOEPin::high();                     // no OE
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint16_t readCPUBusAddress() {
  CPUABufHOEPin::low();                     // enable high byte

  uint16_t laddress = readNEOBus();         // read it
  laddress = laddress << 8u;                // align to high byte

  CPUABufHOEPin::high();                    // disable hight byte
  CPUABufLOEPin::low();                     // enable low byte

  laddress |= readNEOBus();                 // read it

  CPUABufLOEPin::high();                    // disable low byte

  return laddress;
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint16_t readCPUAddress() {
  if (getControlMode() == mRPI) {
    CPUARegOEPin::low();                    // output AREG address
  }

  CPUABufHOEPin::low();     // enable high byte

  uint16_t laddress = readNEOBus(); // read it
  laddress = laddress << 8u;        // align to high byte

  CPUABufHOEPin::high();    // disable hight byte
  CPUABufLOEPin::low();     // enable low byte

  laddress |= readNEOBus();  // read it

  CPUABufLOEPin::high();   // disable low byte

  if (getControlMode() == mRPI) {
    CPUARegOEPin::high();    // disable AREG output
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
  CPUARegHLatchPin::low();  // arm latch

  delayNs<60>();

  CPUARegHLatchPin::high(); // latch in AREG

  resetNEOBus();            // reset NEObus
}

/// <summary>
/// write Address A00..07
/// </summary>
/// <param name="vAddress"></param>
static void writeCPUAddressL(const uint8_t vAddress) {
  // set LowAddress A0..7
  writeNEOBus(vAddress);   // write hight byte of address on NEObus
  CPUARegLLatchPin::low(); // arm latch

  delayNs<60>();

  CPUARegLLatchPin::high(); // latch in AREG

  resetNEOBus();           // reset NEObus
}

/// <summary>
/// latch CPU address in AREG
/// </summary>
/// <param name="vAddress"></param>
static void writeCPUAddress(const uint16_t vAddress) {
  if (getControlMode() == mRPI) {         // only in MMU mode
    writeCPUAddressH(vAddress >> 8);      // latch AddressH
    writeCPUAddressL(vAddress & 0xFF);    // latch AddressL

#if USE_VALIDATION
    uint16_t laddress = readCPUAddress(); // validate
    if (laddress != vAddress) {
      Serial1.printf("*E writeCPUAddress: 0x%04X (0x%04X)\n", vAddress, laddress);
    }
#endif
  }
  else
    Serial1.printf("*E: setCPUAddress: wrong mode\n");

}

/// <summary>
/// read cycle on 6502 databus
/// </summary>
/// <returns></returns>
//inline __attribute__((always_inline))
uint8_t read6502Data() {
  // read databus
  MRWPin::high(); // to be sure
  CPUDBufOEPin::low();  // read from databus

  delayNs<50>();

  uint8_t ldata = readNEOBus();  // read data

  CPUDBufOEPin::high();         // disable data output

  return ldata;
}

/// <summary>
/// read CPU memory location
/// </summary>
/// <param name="vAddress"></param>
/// <returns></returns>
uint8_t read6502Memory(const uint16_t vAddress) {
  if (getControlMode() == mRPI) {
    set6502RW(mREAD);       // set RW to read

    writeCPUAddress(vAddress); // latch address

    CPUARegOEPin::low();    // output address on cpu bus

    uint8_t ldata = read6502Data(); // read data from bus

    CPUARegOEPin::high();   // disable address output

    return ldata;
  }
  else
    Serial1.println("*E: write6502Meory: wrong mode");

  return 0;
}

/// <summary>
/// write CPU memory location
/// </summary>
/// <param name="vAddress"></param>
/// <param name="vData"></param>
void write6502Memory(const uint16_t vAddress, const uint8_t vData) {
  if (getControlMode() == mRPI) {
    disableMMUInterrupt();

    writeCPUAddress(vAddress);  // latch address

    CPUARegOEPin::low();        // output on address bus

    writeNEOBus(vData);         // write data to Neodbus

    MRWPin::low();              // write cycle
    set6502RW(mWRITE);          // set RW to write
    CPUDBufOEPin::low();        // enable databus

    delayNs<60>();

    set6502RW(mREAD);           // set RW to read for next cycle
    MRWPin::high();             // end write cycle
    CPUDBufOEPin::high();       // disable databus

#if USE_VALIDATION
    uint8_t ldata = read6502Memory(vAddress);  // validate

    CPUARegOEPin::high();      // disable address output

    if (ldata != vData) {
      Serial1.printf("*E: write6502Memory: 0x%04X: 0x%02X (0x%02X)\n", vAddress, vData, ldata);
    }
#else
    CPUARegOEPin::high();      // disable address output

#endif

    enableMMUInterrupt();
  }
  else
    Serial1.println("*E: write6502Memory: wrong mode");
}

/// <summary>
/// Snoop read from memory, halting the possibly running CPU
/// </summary>
/// <param name="vAddress"></param>
/// <param name="vBytes"></param>
/// <param name="vBuffer"></param>
void snoop_read6502Memory(const uint16_t vAddress, const uint32_t vBytes, uint8_t* vBuffer) {
  uint8_t lState = get6502State();
  set6502State(sRPI);

  uint16_t lAd = vAddress;
  uint8_t* lBuf = (uint8_t*)vBuffer;
  for (uint16_t m = 0; m < vBytes; m++) {
    lBuf[m] = read6502Memory(lAd++);
  }

  set6502State(lState);  // return to prev state
}

/// <summary>
/// Snoop write to memory, halting the possible running CPU
/// </summary>
/// <param name="vAddress"></param>
/// <param name="vBytes"></param>
/// <param name="vBuffer"></param>
void snoop_write6502Memory(const uint16_t vAddress, uint32_t vBytes, const uint8_t* vBuffer) {
  uint8_t lState = get6502State();
  set6502State(sRPI);

  uint16_t lAd = vAddress;
  for (uint16_t m = 0; m < vBytes; m++) {
    write6502Memory(lAd++, vBuffer[m]);
  }

  set6502State(lState);  // return to prev state
}

#if 0
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary>
/// 
/// </summary>
void testBUS() {
  uint8_t vData, lData;

  uint16_t lAddress = random(0x0200, 0XEFFF);

  vData = random(0xFF);

  setDebug(mLOW);

  write6502Memory(lAddress, vData);

  setDebug(mHIGH);

  setDebug(mLOW);

  read6502Memory(random(0x0200, 0XEFFF));
  read6502Memory(random(0x0200, 0XEFFF));
  lData = read6502Memory(lAddress);

  setDebug(mHIGH);

  if (vData != lData)
    Serial1.printf("*E: testBus: error %04X : %02X (%02X)\n", lAddress, vData, lData);
}
#endif
