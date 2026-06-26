#pragma once

#include <Arduino.h>
#include "usb_storage.h"

// ============================================================
// usb_msc_diskio.h
// NEO MMU - FatFs disk I/O glue for TinyUSB MSC host
//
// Physical drive N maps to USB MSC storage slot N. RP FS/mailbox open calls
// can select the target device, and FatFs can mount multiple drives at once.
//
// Arduino-Pico 5.6.x wraps Elm-Chan FatFs symbols in namespace fatfs.
// Therefore the diskio callbacks must also be declared/defined in fatfs.
// ============================================================

#ifndef USB_MSC_DISKIO_MAX_PDRV
#define USB_MSC_DISKIO_MAX_PDRV USB_STORAGE_MAX_DEVICES
#endif

// Read-only milestone. Enable writes only after read/mount/open/read is stable.
#ifndef USB_MSC_DISKIO_READONLY
#define USB_MSC_DISKIO_READONLY 1
#endif

// Local project FatFs. Include ff.h first because diskio.h depends on BYTE/UINT/LBA_t.
#include "fatfs_local/ff.h"
#include "fatfs_local/diskio.h"

