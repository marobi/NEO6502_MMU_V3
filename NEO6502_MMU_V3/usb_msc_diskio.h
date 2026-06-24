#pragma once

#include <Arduino.h>

// ============================================================
// usb_msc_diskio.h
// NEO MMU - FatFs disk I/O glue for TinyUSB MSC host
//
// Physical drive 0 maps to the currently mounted USB MSC device/LUN.
// This layer is deliberately block-only. Filesystem policy lives above it.
//
// Arduino-Pico 5.6.x wraps Elm-Chan FatFs symbols in namespace fatfs.
// Therefore the diskio callbacks must also be declared/defined in fatfs.
// ============================================================

#ifndef USB_MSC_DISKIO_PDRV
#define USB_MSC_DISKIO_PDRV 0
#endif

// Read-only milestone. Enable writes only after read/mount/open/read is stable.
#ifndef USB_MSC_DISKIO_READONLY
#define USB_MSC_DISKIO_READONLY 1
#endif

#if __has_include("ff.h") && __has_include("diskio.h")
  // Arduino-Pico FatFS diskio.h depends on FatFs base types from ff.h.
  // Include ff.h first or BYTE/UINT/LBA_t are not visible when diskio.h is parsed.
  #include "ff.h"
  #include "diskio.h"
#else
  // Minimal fallback definitions only for builds where FatFs headers are not
  // available. The real Arduino-Pico FatFS library provides these in namespace
  // fatfs, so keep the fallback namespace-compatible.
  namespace fatfs {
    typedef uint8_t  BYTE;
    typedef uint16_t WORD;
    typedef uint32_t DWORD;
    typedef unsigned int UINT;
    typedef uint32_t LBA_t;
    typedef BYTE DSTATUS;

    enum DRESULT {
      RES_OK = 0,
      RES_ERROR,
      RES_WRPRT,
      RES_NOTRDY,
      RES_PARERR
    };

    DSTATUS disk_initialize(BYTE pdrv);
    DSTATUS disk_status(BYTE pdrv);
    DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);
    DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count);
    DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff);
  }

  #ifndef STA_NOINIT
  #define STA_NOINIT  0x01
  #define STA_NODISK  0x02
  #define STA_PROTECT 0x04
  #endif

  #ifndef CTRL_SYNC
  #define CTRL_SYNC        0
  #define GET_SECTOR_COUNT 1
  #define GET_SECTOR_SIZE  2
  #define GET_BLOCK_SIZE   3
  #endif
#endif

