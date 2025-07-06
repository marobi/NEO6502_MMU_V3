// control.h

#ifndef _CONTROL_h
#define _CONTROL_h

#include <arduino.h>

#define DELAY_FACTOR_SHORT() asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n");

// neo bus
constexpr auto DATA_BUS_MASK = (0xFF0000000000LL); // pin 40..47

uint8_t getControlMode();

void setDebug(const uint8_t);

void setControlMode(const uint8_t);

void setmRW(const uint8_t);

uint8_t readNEOBus();

void writeNEOBus(const uint8_t);

void resetNEOBus();

void setupControl();

#endif
