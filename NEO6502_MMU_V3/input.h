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
#include "fifo.h"

extern FIFO<uint8_t, 128>  cpu_tx_fifo;         // FIFO to write to CPU

extern FIFO<uint8_t, 128>  vdu_tx_fifo;         // FIFO to write to VDU/user output

bool writeCPUQ(const uint8_t c);
uint8_t readCPUQ();
bool isEmptyCPUQ();
bool writeVDUQ(const uint8_t c);

void initInput();
void taskInput();
