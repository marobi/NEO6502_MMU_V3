#pragma once
#include "fifo.h"

bool writeCPUQ(const uint8_t c);
bool writeVDUQ(const uint8_t c);

void inpInit();
void inpExecute();
