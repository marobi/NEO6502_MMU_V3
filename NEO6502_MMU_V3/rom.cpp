// 
// 
// 
#include <arduino.h>
#include "rom.h"
#include "neobus.h"
#include "ram.h"

/// <summary>
/// 
/// </summary>
typedef struct {            // 16 bytes
  uint8_t SOH;              // fixed 0x5A
  uint8_t VERSION_MINOR;    // 0x01
  uint8_t VERSION_MAJOR;    // 0x01
  uint8_t STARTADDRESS_L;   // load address
  uint8_t STARTADDRESS_H;
  uint8_t SIZE_L;           // ROM size
  uint8_t SIZE_H;
  uint8_t TYPE;             // ROM type
  uint8_t NMI_L;            // NMI vector
  uint8_t NMI_H;
  uint8_t RESET_L;          // RESET vector
  uint8_t RESET_H;
  uint8_t IRQ_L;            // IRQ vector
  uint8_t IRQ_H;
  uint8_t CSUM;             // header checksum
  uint8_t EOH;              // fixed 0xA5
} defROM;

/// <summary>
/// load a ROM image into memory
/// </summary>
/// <param name="vRom"></param>
/// <returns></returns>
bool loadROM(const uint8_t* vROM) {
  uint16_t startAddress;
  uint16_t romSize;

  defROM* hdr = (defROM*)vROM;

  if ((hdr->SOH != 0x5A) || (hdr->EOH != 0xA5)) {
    Serial.println("*E: Invalid ROM header");
    return false;
  }

  if (hdr->VERSION_MAJOR != 0x01) {
    Serial.println("*E: Invalid ROM version");
    return false;
  }

  // calc csum
  uint8_t csum = hdr->STARTADDRESS_L;
  csum += hdr->STARTADDRESS_H;
  csum += hdr->SIZE_L;
  csum += hdr->SIZE_H;
  csum += hdr->TYPE;
  csum += hdr->NMI_L;
  csum += hdr->NMI_H;
  csum += hdr->RESET_L;
  csum += hdr->RESET_H;
  csum += hdr->IRQ_L;
  csum += hdr->IRQ_H;

  if (csum != hdr->CSUM) {
    Serial.println("*E: Invalid checksum");
    return false;
  }

  //    Serial.printf("ROM: 0x%02x\n", hdr->TYPE);
  if ((hdr->TYPE & 0x01) != 0) {
    // set NMI
    snoop_write6502Memory(0xFFFA, 1, &hdr->NMI_L);
    snoop_write6502Memory(0xFFFB, 1, &hdr->NMI_H);
    Serial.printf("*D: NMIVEC: 0x%02x%02x\n", hdr->NMI_H, hdr->NMI_L);
  }
  if ((hdr->TYPE & 0x02) != 0) {
    // set RESET
    snoop_write6502Memory(0xFFFC, 1, &hdr->RESET_L);
    snoop_write6502Memory(0xFFFD, 1, &hdr->RESET_H);
    Serial.printf("*D: RSTVEC: 0x%02x%02x\n", hdr->RESET_H, hdr->RESET_L);
  }
  if ((hdr->TYPE & 0x04) != 0) {
    // set IRQ
    snoop_write6502Memory(0xFFFE, 1, &hdr->IRQ_L);
    snoop_write6502Memory(0xFFFF, 1, &hdr->IRQ_H);
    Serial.printf("*D: IRQVEC: 0x%02x%02x\n", hdr->IRQ_H, hdr->IRQ_L);
  }

  startAddress = (uint16_t)hdr->STARTADDRESS_H * 256 + hdr->STARTADDRESS_L;
  romSize = (uint16_t)hdr->SIZE_H * 256 + hdr->SIZE_L;
  Serial.printf("*D: %s\t%04X: [%04X]\n", "ROM", startAddress, romSize);

  return loadBinary(startAddress, romSize, vROM + sizeof(defROM));
}
