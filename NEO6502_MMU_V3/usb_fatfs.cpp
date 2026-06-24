#include <Arduino.h>
#include "usb_fatfs.h"
#include "usb_storage.h"
#include "rp_fs.h"

// ============================================================
// usb_fatfs.cpp
// NEO MMU - FatFs mount/unmount over TinyUSB MSC diskio
//
// FatFs is a required dependency for this module. If ff.h is not in the
// Visual Micro/Arduino build include path, the build must fail.
// ============================================================

#include <ff.h>

using namespace fatfs;

static FATFS gUSBFatFs;
static bool gUSBFatFsMounted = false;

bool usb_fatfs_available() {
  return true;
}

bool usb_fatfs_mounted() {
  return gUSBFatFsMounted;
}

bool usb_fatfs_mount() {
  if (gUSBFatFsMounted)
    return true;

  if (!usb_storage_device_mounted()) {
    Serial1.println("*E: USB FatFs: no MSC device mounted");
    return false;
  }

  Serial1.println("*I: USB FatFs: mounting drive 0:");
  FRESULT const fr = f_mount(&gUSBFatFs, "0:", 1);
  if (fr != FR_OK) {
    Serial1.printf("*E: USB FatFs: f_mount failed fr=%u\n", (unsigned)fr);
    return false;
  }

  gUSBFatFsMounted = true;
  Serial1.println("*I: USB FatFs mounted");
  return true;
}

void usb_fatfs_unmount() {
  if (gUSBFatFsMounted) {
    rp_fs_close_all();
    f_mount(nullptr, "0:", 0);
    gUSBFatFsMounted = false;
    Serial1.println("*I: USB FatFs unmounted");
  }
}

namespace fatfs {
DWORD get_fattime(void) {
  // Fixed timestamp: 2026-01-01 00:00:00. Read-only milestone does not rely
  // on this, but FatFs may require the symbol when write support is compiled.
  return ((DWORD)(2026 - 1980) << 25) |
         ((DWORD)1 << 21) |
         ((DWORD)1 << 16);
}
} // namespace fatfs
