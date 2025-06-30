// mmu.h

#ifndef _MMU_h
#define _MMU_h

#include "pins.h"

//#define PAGE_SIZE        (4096)      // 4k
#define NUM_CONTEXTS       (128)       // 128 x 4k = 512k
#define MUM_CONTEXT_PAGES  (16)        // 16 x 4k = 64k
#define DEFAULT_CONTEXT    (0)


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

void testMMU();

#endif

