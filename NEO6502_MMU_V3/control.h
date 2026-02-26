// control.h

#pragma once
#include <Arduino.h>

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

void setDebug(const bool);

void setControlMode(const uint8_t);

void setmRW(const bool);

uint8_t readNEOBus();

void writeNEOBus(const uint8_t);

void resetNEOBus();

void setupControl();

