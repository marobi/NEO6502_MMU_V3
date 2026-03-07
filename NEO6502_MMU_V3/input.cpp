#include "Arduino.h"
#include "input.h"
#include "vdu.h"
#include "cmd.h"

FIFO<uint8_t, 64> cpu_tx_fifo;          // FIFO to write to CPU

FIFO<uint8_t, 64> vdu_tx_fifo;          // FIFO to write to VDU/user output

/// <summary>
/// inpInit initializes the input system by clearing all the FIFOs used for communication
/// This ensures that any residual data from previous operations is removed, 
/// providing a clean state for the system to start processing new inputs and outputs.
/// </summary>
void inpInit() {
  cpu_tx_fifo.clear();
  vdu_tx_fifo.clear();
}

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
/// <returns></returns>
bool writeCPUQ(const uint8_t c) {
  if (!cpu_tx_fifo.isFull()) {
    cpu_tx_fifo.push(c);
    return true;
  }
  else {
//    Serial1.println("*E: inpWriteCPUQ: FIFO full");
    return false;
  }
}

/// <summary>
/// process FIFO KB -> CPU TX
/// </summary>
bool writeVDUQ(const uint8_t c) {
  if (!vdu_tx_fifo.isFull()) {
    vdu_tx_fifo.push(c);
    return true;
  }
  else {
//    Serial1.println("*E: inpWriteVDUQ: FIFO full");
    return false;
  }
}

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

/// <summary>
/// process FIFO VDU 
/// </summary>
static void processVDUoutQ() {
  uint8_t data = 0;

  if (! vdu_tx_fifo.isEmpty()) {     // process all pending vdu data
    vdu_tx_fifo.pop(data);           // get data from fifo
    vduPutc(data);                   // send to VDU
//    Serial1.printf("*D: processVDUoutQ: [%02x]\n", data);
  }
}

/// <summary>
/// read from CPU, output to VDU
/// </summary>
static void processCPUinQ() {
  if (!vdu_tx_fifo.isFull()) {
    uint8_t data = inChar6502();
    if (data > 0) {
      writeVDUQ(data);
//      Serial1.printf("*D: processCPUinQ: [%02x]\n", data);
    }
  }
}

/// <summary>
/// 
/// </summary>
void inpExecute() {
  processCPUoutQ();
  processCPUinQ();
  processVDUoutQ();
}
