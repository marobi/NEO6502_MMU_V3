#pragma once

#include <Arduino.h>

// ============================================================
// usb_storage.h
// NEO MMU - RP2350 USB host storage service
//
// Storage stack:
//   - Adafruit TinyUSB native USB host controller
//   - USB MSC attach/remove handling
//   - Raw block I/O for FatFs diskio
//
// Filesystem parsing is handled by FatFs. RP-local file handles live in
// rp_fs.*. This module does not contain a private FAT32/MBR parser and does
// not implement the 6502 mailbox filesystem API yet.
// ============================================================

void initUSBStorage();
void taskUSBStorage();

bool usb_storage_host_enabled();
bool usb_storage_device_mounted();
bool usb_storage_ready();

uint8_t  usb_storage_device_address();
uint8_t  usb_storage_lun();
uint32_t usb_storage_block_count();
uint32_t usb_storage_block_size();

// Raw block access used by the FatFs diskio layer. These functions are not
// user/monitor commands and are not the 6502 mailbox filesystem API.
bool usb_storage_read_blocks(uint32_t lba, uint8_t* buffer, uint16_t count, uint32_t timeout_ms);
bool usb_storage_write_blocks(uint32_t lba, const uint8_t* buffer, uint16_t count, uint32_t timeout_ms);
bool usb_storage_sync();

// Temporary RP-local status printer used by startup/logging only. This is not
// registered as a monitor command.
void usb_storage_print_summary();
