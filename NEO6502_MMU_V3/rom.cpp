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
/// Validate a ROM cartridge header and return its load address and payload size.
/// </summary>
/// <param name="hdr">ROM cartridge header.</param>
/// <param name="startAddress">Receives the 6502 load address.</param>
/// <param name="romSize">Receives the ROM payload size.</param>
/// <returns>True when the header is valid.</returns>
static bool validateROMHeader(const defROM& hdr, uint16_t* startAddress, uint16_t* romSize) {
  if ((hdr.SOH != 0x5A) || (hdr.EOH != 0xA5)) {
    Serial1.println("*E: validateROMHeader: Invalid ROM header");
    return false;
  }

  if (hdr.VERSION_MAJOR != 0x01) {
    Serial1.println("*E: validateROMHeader: Invalid ROM version");
    return false;
  }

  uint8_t csum = hdr.STARTADDRESS_L;
  csum += hdr.STARTADDRESS_H;
  csum += hdr.SIZE_L;
  csum += hdr.SIZE_H;
  csum += hdr.TYPE;
  csum += hdr.NMI_L;
  csum += hdr.NMI_H;
  csum += hdr.RESET_L;
  csum += hdr.RESET_H;
  csum += hdr.IRQ_L;
  csum += hdr.IRQ_H;

  if (csum != hdr.CSUM) {
    Serial1.println("*E: validateROMHeader: Invalid checksum");
    return false;
  }

  *startAddress = (uint16_t)hdr.STARTADDRESS_H * 256 + hdr.STARTADDRESS_L;
  *romSize = (uint16_t)hdr.SIZE_H * 256 + hdr.SIZE_L;

  if (((uint32_t)*startAddress + (uint32_t)*romSize) > 0x10000UL) {
    Serial1.printf("*E: validateROMHeader: ROM @0x%04X with size 0x%04X does not fit\n",
      *startAddress, *romSize);
    return false;
  }

  return true;
}

/// <summary>
/// Install cartridge vectors after the complete ROM payload has loaded.
/// </summary>
/// <param name="hdr">Validated ROM cartridge header.</param>
static void installROMVectors(const defROM& hdr) {
  if ((hdr.TYPE & 0x01) != 0) {
    write6502Memory(0xFFFA, hdr.NMI_L);
    write6502Memory(0xFFFB, hdr.NMI_H);
  }
  if ((hdr.TYPE & 0x02) != 0) {
    write6502Memory(0xFFFC, hdr.RESET_L);
    write6502Memory(0xFFFD, hdr.RESET_H);
  }
  if ((hdr.TYPE & 0x04) != 0) {
    write6502Memory(0xFFFE, hdr.IRQ_L);
    write6502Memory(0xFFFF, hdr.IRQ_H);
  }
}

/// <summary>
/// Load a ROM cartridge file directly into the current MMU context.
/// The payload is streamed in fixed-size chunks and no full-file allocation is used.
/// </summary>
/// <param name="path">LittleFS path of the ROM cartridge file.</param>
/// <returns>True when the complete cartridge was validated and loaded.</returns>
bool loadROMCartridgeFile(const char* path) {
  static const size_t LOAD_CHUNK_SIZE = 512;
  uint8_t buffer[LOAD_CHUNK_SIZE];
  defROM hdr;
  uint16_t startAddress = 0;
  uint16_t romSize = 0;
  uint32_t offset = 0;

  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial1.printf("*E: loadROMCartridgeFile: cannot open %s\n", path);
    return false;
  }

  const size_t fileSize = file.size();
  if (fileSize < sizeof(defROM)) {
    Serial1.printf("*E: loadROMCartridgeFile: %s too small\n", path);
    file.close();
    return false;
  }

  const size_t headerRead = file.read((uint8_t*)&hdr, sizeof(hdr));
  if (headerRead != sizeof(hdr)) {
    Serial1.printf("*E: loadROMCartridgeFile: %s header read error\n", path);
    file.close();
    return false;
  }

  if (!validateROMHeader(hdr, &startAddress, &romSize)) {
    file.close();
    return false;
  }

  const size_t expectedFileSize = sizeof(defROM) + (size_t)romSize;
  if (fileSize != expectedFileSize) {
    Serial1.printf("*E: loadROMCartridgeFile: %s size mismatch (%lu != %lu)\n",
      path, (unsigned long)fileSize, (unsigned long)expectedFileSize);
    file.close();
    return false;
  }

  vduPrintf("%04X %04X\n", startAddress, romSize);

  while (offset < romSize) {
    const size_t remaining = (size_t)romSize - (size_t)offset;
    const size_t requested = remaining < LOAD_CHUNK_SIZE ? remaining : LOAD_CHUNK_SIZE;
    const size_t bytesRead = file.read(buffer, requested);

    if (bytesRead != requested) {
      Serial1.printf("*E: loadROMCartridgeFile: %s read error at 0x%04lX\n",
        path, (unsigned long)offset);
      file.close();
      return false;
    }

    for (size_t i = 0; i < bytesRead; i++) {
      write6502Memory((uint16_t)((uint32_t)startAddress + offset + i), buffer[i]);
    }

    offset += bytesRead;
  }

  file.close();
  installROMVectors(hdr);
  return true;
}
