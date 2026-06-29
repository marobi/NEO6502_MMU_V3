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

#include <LittleFS.h>
#include "rom.h"
#include "neobus.h"
#include "ram.h"
#include "vdu.h"

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
bool loadROMCartridge(const uint8_t* vCartridge) {
  uint16_t startAddress;
  uint16_t romSize;

  defROM* hdr = (defROM*)vCartridge;

  if ((hdr->SOH != 0x5A) || (hdr->EOH != 0xA5)) {
    Serial1.println("*E: loadROMCartridge: Invalid ROM header");
    return false;
  }

  if (hdr->VERSION_MAJOR != 0x01) {
    Serial1.println("*E: loadROMCartridge: Invalid ROM version");
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
    Serial1.println("*E: loadROMCartridge: Invalid checksum");
    return false;
  }

  startAddress = (uint16_t)hdr->STARTADDRESS_H * 256 + hdr->STARTADDRESS_L;
  romSize = (uint16_t)hdr->SIZE_H * 256 + hdr->SIZE_L;
  vduPrintf("%04X %04X\n", startAddress, romSize);

  if (!loadBinary(startAddress, romSize, vCartridge + sizeof(defROM))) {
    Serial1.println("*E: loadROMCartridge: Failed to load ROM into memory");
    return false;
  }

  //    Serial1.printf("ROM: 0x%02x\n", hdr->TYPE);
  if ((hdr->TYPE & 0x01) != 0) {
    // set NMI
    write6502Memory(0xFFFA, hdr->NMI_L);
    write6502Memory(0xFFFB, hdr->NMI_H);
//    Serial1.printf("*I: loadROMCartridge: NMIVEC: 0x%02x%02x\n", hdr->NMI_H, hdr->NMI_L);
  }
  if ((hdr->TYPE & 0x02) != 0) {
    // set RESET
    write6502Memory(0xFFFC, hdr->RESET_L);
    write6502Memory(0xFFFD, hdr->RESET_H);
//    Serial1.printf("*I: loadROMCartridge: RSTVEC: 0x%02x%02x\n", hdr->RESET_H, hdr->RESET_L);
  }
  if ((hdr->TYPE & 0x04) != 0) {
    // set IRQ
    write6502Memory(0xFFFE, hdr->IRQ_L);
    write6502Memory(0xFFFF, hdr->IRQ_H);
//    Serial1.printf("*I: loadROMCartridge: IRQVEC: 0x%02x%02x\n", hdr->IRQ_H, hdr->IRQ_L);
  }

  return true;
}

/// <summary>
/// 
/// </summary>
/// <param name="path"></param>
/// <param name="outSize"></param>
/// <returns></returns>
uint8_t* readBinaryFile(const char* path) {
  File file = LittleFS.open(path, "r");
  if (!file)
    return nullptr;

  size_t fileSize = file.size();
  if (fileSize < 16) {
    file.close();
    Serial1.printf("*E: readBinaryFile: %s too small\n", path);
    return nullptr;
  }

  uint8_t* buffer = (uint8_t*)malloc(fileSize);
  if (!buffer) {
    file.close();
    Serial1.printf("*E: readBinaryFile: %s cannot allocate\n", path);
    return nullptr;
  }

  size_t totalRead = 0;

  while (totalRead < fileSize) {
    size_t n = file.read(buffer + totalRead, fileSize - totalRead);
    if (n == 0) {
      free(buffer);
      file.close();
      Serial1.printf("*E: readBinaryFile: %s read error\n", path);
      return nullptr;
    }
    totalRead += n;
  }

  file.close();

  return buffer;
}
