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
#include "Arduino.h"
#include "input.h"
#include "vdu.h"
#include "cmd.h"

FIFO<uint8_t, 128> cpu_tx_fifo;         // FIFO to write to CPU

FIFO<uint8_t, 128> vdu_tx_fifo;         // FIFO to write to VDU/user output

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
/// <returns></returns>
bool writeCPUQ(const uint8_t c) {
  if (! cpu_tx_fifo.isFull()) {
    cpu_tx_fifo.push(c);
    return true;
  }
  else {
    Serial1.println("*E: writeCPUQ: FIFO full");
    return false;
  }
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
uint8_t readCPUQ() {
  uint8_t c = 0;

  if (! cpu_tx_fifo.isEmpty()) {
    cpu_tx_fifo.pop(c);
    return c;
  }
  return 0;
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
bool isEmptyCPUQ() {
  return cpu_tx_fifo.isEmpty();
}

/// <summary>
/// process FIFO -> VDU
/// </summary>
bool writeVDUQ(const uint8_t c) {  
  if (c != 0x00)
    vdu_tx_fifo.push(c);
  return true;
}

/// <summary>
/// isFullVDUQ checks if the VDU output FIFO is full
/// </summary>
/// <returns></returns>
bool isFullVDUQ() {
  return vdu_tx_fifo.isFull();
}

#if 0
/// <summary>
/// process FIFO CPU 
/// </summary>
static void processCPUoutQ() {
  uint8_t data = 0;

  if (!cpu_tx_fifo.isEmpty()) {    // process all pending cpu data
    if (outCharAvailable6502()) {  // check if cpu is ready to receive char
      if (cpu_tx_fifo.pop(data)) { // get data from cpu fifo
        outChar6502(data);         // send to cpu
        //        Serial1.printf("*D: processCPUoutQ: [%02x]\n", data);
      }
    }
  }
}
#endif

/// <summary>
/// process FIFO VDU 
/// </summary>
static void processVDUoutQ() {
  uint8_t data = 0;

  while (!vdu_tx_fifo.isEmpty()) {  // process all pending vdu data
    vdu_tx_fifo.pop(data);           // get data from fifo
    vduPutc(data);                   // send to VDU
    //    Serial1.printf("*D: processVDUoutQ: [%02x]\n", data);
  }
}


#if 0
/// <summary>
/// read from CPU, output to VDU
/// </summary>
static void processCPUinQ() {
  if (!vdu_tx_fifo.isFull()) {
    uint8_t data = inChar6502();
    if (data != 0) {
      writeVDUQ(data);
      //      Serial1.printf("*D: processCPUinQ: [%02x]\n", data);
    }
  }
}
#endif

/// <summary>
/// inpInit initializes the input system by clearing all the FIFOs used for communication
/// This ensures that any residual data from previous operations is removed, 
/// providing a clean state for the system to start processing new inputs and outputs.
/// </summary>
void initInput() {
  cpu_tx_fifo.clear();
  vdu_tx_fifo.clear();
}

/// <summary>
/// 
/// </summary>
void taskInput() {
//  processCPUoutQ();
//  processCPUinQ();
  processVDUoutQ();
}
