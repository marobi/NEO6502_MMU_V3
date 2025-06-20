// mmu.h

#ifndef _MMU_h
#define _MMU_h

#include "pins.h"

#define NUM_CONTEXTS (1)
#define MUM_CONTEXT_PAGES  (16)


uint32_t getMMUIOCount();

void writeMMUContext(const uint8_t);

bool getMMUIO();

bool defMMUContext(const uint8_t, const uint8_t *);

void setupMMU();

uint8_t readMMUPage(const uint8_t, const uint8_t);

bool writeMMUPage(const uint8_t, const uint8_t, const uint8_t);

void dumpMMUContext(const uint8_t);

bool initMMU();

void testMMU();

#endif

