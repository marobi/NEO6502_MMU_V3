#pragma once

// Placeholder BIOS ROM image
static const uint8_t bios_bin[] = {
    0x5A, 0x01, 0x01,        // SOH, minor, major
    0x00, 0x02,              // start address 0x0200
    0x02, 0x00,              // size 2 bytes
    0x06,                    // type: RESET+IRQ vectors
    0x00, 0x00,              // NMI vector
    0x00, 0x02,              // RESET vector
    0x00, 0x02,              // IRQ vector
    0x0E,                    // checksum
    0xA5,                    // EOH
    // Code bytes
    0xEA, 0xEA               // NOP; NOP
};

