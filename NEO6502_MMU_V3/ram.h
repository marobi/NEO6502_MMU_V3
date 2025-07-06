#pragma once

void dumpMemory(const uint16_t vStartAddress, const uint16_t vEndAddress);

bool loadBinary(const uint16_t vAddress, const uint16_t vSize, const uint8_t* vBinary);
