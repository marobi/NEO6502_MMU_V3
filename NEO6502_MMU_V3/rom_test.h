#pragma once

// Simple test ROM image with minimal program
static const uint8_t test_bin[] = {
    0x5A, 0x01, 0x01,        // SOH, minor, major
    0x00, 0x02,              // start address 0x0200
    0x01, 0x00,              // size 1 byte
    0x06,                    // type: RESET+IRQ vectors
    0x00, 0x00,              // NMI vector
    0x00, 0x02,              // RESET vector
    0x00, 0x02,              // IRQ vector
    0x0D,                    // checksum
    0xA5,                    // EOH
    // Code bytes
    0xEA                     // NOP
};

