// 
// 
// 
#include <Arduino.h>
#include "simple_io.h"
#include "neobus.h"
#include "mmu.h"
#include "mailbox.h"
#include "input.h"

/// <summary>
/// simple_getchar checks if the CPU is ready to receive input by reading a specific memory location.
/// </summary>
/// <returns></returns>
void simple_getchar() {
  if (snoop_read6502MemoryLoc(RP_GETCHAR) == 0x00) { // check if CPU is ready to receive input
    uint8_t c = readCPUQ();                          // read from CPUQ, non-blocking

    if (c != 0x00) {                                 // if we got a char, write it to the CPU memory location
      snoop_write6502MemoryLoc(RP_GETCHAR, c);       // and set it for the CPU to read
    }
  }
}

/// <summary>
/// simple_putchar checks if the CPU has written a char to the memory location, 
/// and if so, writes it to the VDU and resets the memory location for the next char. 
/// This allows the CPU to output characters to the VDU by writing to a specific memory location.
/// </summary>
/// <param name="c"></param>
void simple_putchar() {
  uint8_t c = snoop_read6502MemoryLoc(RP_PUTCHAR);
  if (c != 0x00) {                                // check if CPU has written a char to the memory location
    writeVDUQ(c);                                 // write it to the VDU
    snoop_write6502MemoryLoc(RP_PUTCHAR, 0x00);   // and reset the memory location for the next char
  }
}

/// <summary>
/// task to handle simple I/O, checking for input from the CPU and output to the VDU.
/// </summary>
void taskSimpleIO() {
  if (getMMUContext() == 0) { // only if context is 0, otherwise simple I/O is not active
    simple_getchar();
    simple_putchar();

    delay(1); // delay to avoid hogging the CPU
  }  
}

/// <summary>
/// initialize simple I/O by setting the memory locations for getchar and putchar to 0,
/// </summary>
void initSimpleIO() {
  snoop_write6502MemoryLoc(RP_GETCHAR, 0x00); // initialize memory locations
  snoop_write6502MemoryLoc(RP_PUTCHAR, 0x00);
}
