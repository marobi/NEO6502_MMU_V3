// cpu.h

#ifndef _BUS_h
#define _BUS_h

#include "pins.h"

void setCPUARegOE(const uint8_t vHL);

void setupCPU();

uint16_t readCPUAddress();

void writeCPUAddressH(const uint8_t);

uint8_t read6502Data();

uint8_t read6502Memory(const uint16_t);

void write6502Memory(const uint16_t, const uint8_t);

void snoop_read6502Memory(const uint16_t, const uint16_t, uint8_t*);

void snoop_write6502Memory(const uint16_t, uint16_t, const uint8_t*);

#if 0
void testBUS();
#endif

#endif