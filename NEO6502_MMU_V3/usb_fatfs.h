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
