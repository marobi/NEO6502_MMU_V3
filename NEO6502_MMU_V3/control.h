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
#include <Arduino.h>

#include "delay.h"

// control mode
typedef enum {
  mRPI = 0,
  mCPU
} controlmode_t;

controlmode_t getControlMode();

void setControlMode(const controlmode_t);

uint8_t readNEOBus();

void writeNEOBus(const uint8_t);

void resetNEOBus();

void setupControl();
