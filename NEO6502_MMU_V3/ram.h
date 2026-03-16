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

void dumpMemory(const uint16_t vStartAddress, const uint16_t vEndAddress);

bool loadBinary(const uint16_t vAddress, const uint16_t vSize, const uint8_t* vBinary);

void fillMemory(const uint8_t vVal);
