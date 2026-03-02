// control.h

#pragma once
#include <Arduino.h>

// control mode
#define mRPI  0
#define mCPU  1

#define DELAY_FACTOR_SHORT() \
    asm volatile(            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        "nop\n\t"            \
        ::: "memory")

uint8_t getControlMode();

void setControlMode(const uint8_t);

uint8_t readNEOBus();

void writeNEOBus(const uint8_t);

void resetNEOBus();

void setupControl();
