// 
// 
// 
#include <arduino.h>
#include "config.h"
#include "ram.h"
#include "control.h"
#include "mmu.h"
#include "neobus.h"

/// <summary>
/// dump 16 bytes of memory
/// </summary>
/// <param name="vAddress"></param>
inline __attribute__((always_inline))
void dump16(const uint16_t vAddress) {
  uint8_t dat;

  Serial.printf("%04X:", vAddress);
  for (uint8_t m = 0; m < 8; m++) {
    snoop_read6502Memory(vAddress + m, 1, &dat);
    Serial.printf(" %02X", dat);
  }
  Serial.printf(" ");
  for (uint8_t m = 8; m < 16; m++) {
    snoop_read6502Memory(vAddress + m, 1, &dat);
    Serial.printf(" %02X", dat);
  }

  Serial.println();
}

/// <summary>
/// dump memory
/// </summary>
/// <param name="vStartAddress"></param>
/// <param name="vEndAddress"></param>
void dumpMemory(const uint16_t vStartAddress, const uint16_t vEndAddress) {
  for (uint16_t ad = 0; ad < ((vEndAddress - vStartAddress) / 16) + 1; ad++) {
    dump16(vStartAddress + (ad * 16));
  }

  Serial.println();
}

/// <summary>
/// load a binary at memory addresss with size
/// </summary>
/// <param name="vAddress"></param>
/// <param name="vSize"></param>
/// <param name="vBinary"></param>
/// <returns></returns>
bool loadBinary(const uint16_t vAddress, const uint16_t vSize, const uint8_t* vBinary) {
  if ((vAddress + vSize) < vAddress) {
    Serial.printf("*E: loadBinary: binary does not fit\n");
    return false;
  }

  for (uint16_t m = 0; m < vSize; m++) {
    write6502Memory(vAddress + m, vBinary[m]);
  }

  //snoop_write6502Memory(vAddress, vSize, vBinary);

  return true;
}
