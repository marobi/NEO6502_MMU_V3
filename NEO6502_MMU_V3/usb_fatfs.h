#pragma once

#include <Arduino.h>

// ============================================================
// usb_fatfs.h
// NEO MMU - FatFs mount/unmount over TinyUSB MSC diskio
// ============================================================

bool usb_fatfs_available();
bool usb_fatfs_mounted();
bool usb_fatfs_mount();
void usb_fatfs_unmount();

bool usb_fatfs_mounted(uint8_t device);
bool usb_fatfs_mount(uint8_t device);
void usb_fatfs_unmount(uint8_t device);
uint8_t usb_fatfs_mounted_mask();
uint8_t usb_fatfs_mounted_count();
