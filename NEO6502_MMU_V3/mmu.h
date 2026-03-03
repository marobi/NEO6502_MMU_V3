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
#pragma once

#include "pins.h"

//#define PAGE_SIZE        (4096)      // 4k
#define NUM_CONTEXTS       (128)       // 128 x 4k = 512k
#define NUM_TOTAL_PAGES    (128)       // 128 4k pages available
#define NUM_CONTEXT_PAGES  (16)        // 16 x 4k = 64k
#define DEFAULT_CONTEXT    (0)

extern volatile uint32_t gMMUIOCount;
extern volatile bool gMMUIOTrigger;

uint32_t getMMUIOCount();

uint8_t readMMUContext();

void writeMMUContext(const uint8_t);

bool getMMUIO();

bool defMMUContext(const uint8_t, const uint8_t *);

void setupMMU();

uint8_t readMMUPage(const uint8_t, const uint8_t);

bool writeMMUPage(const uint8_t, const uint8_t, const uint8_t);

void dumpMMUContext(const uint8_t);

bool initMMU();

void disableMMUInterrupt();

void enableMMUInterrupt();

#if 0
void testMMU();
#endif


