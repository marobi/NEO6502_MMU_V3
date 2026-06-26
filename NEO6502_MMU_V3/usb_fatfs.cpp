#include <Arduino.h>
#include "usb_fatfs.h"
#include "usb_storage.h"
#include "rp_fs.h"

// ============================================================
// usb_fatfs.cpp
// NEO MMU - FatFs mount/unmount over TinyUSB MSC diskio
//
// FatFs is built through fatfs_local/ff.cpp so ffconf.h remains project-local.
//
// V28c maps USB storage slot N to FatFs logical drive "N:" and closes RP FS handles per device.
// ============================================================

#include "fatfs_local/ff.h"

#if FF_VOLUMES < USB_STORAGE_MAX_DEVICES
#error "Local FatFs is compiled with too few logical drives. Check fatfs_local/ffconf.h."
#endif

using namespace fatfs;

static FATFS gUSBFatFs[USB_STORAGE_MAX_DEVICES];
static bool gUSBFatFsMounted[USB_STORAGE_MAX_DEVICES];

static bool usb_fatfs_valid_device(uint8_t device) {
  return device < USB_STORAGE_MAX_DEVICES;
}

static bool usb_fatfs_drive_name(uint8_t device, char out[3]) {
  if (!usb_fatfs_valid_device(device) || device > 9 || out == nullptr)
    return false;

  out[0] = (char)('0' + device);
  out[1] = ':';
  out[2] = '\0';
  return true;
}

bool usb_fatfs_available() {
  return true;
}

bool usb_fatfs_mounted() {
  return usb_fatfs_mounted(0);
}

bool usb_fatfs_mounted(uint8_t device) {
  return usb_fatfs_valid_device(device) && gUSBFatFsMounted[device];
}

uint8_t usb_fatfs_mounted_mask() {
  uint8_t mask = 0;
  for (uint8_t device = 0; device < USB_STORAGE_MAX_DEVICES && device < 8; device++) {
    if (usb_fatfs_mounted(device))
      mask |= (uint8_t)(1u << device);
  }
  return mask;
}

uint8_t usb_fatfs_mounted_count() {
  uint8_t count = 0;
  for (uint8_t device = 0; device < USB_STORAGE_MAX_DEVICES; device++) {
    if (usb_fatfs_mounted(device))
      count++;
  }
  return count;
}

bool usb_fatfs_mount() {
  return usb_fatfs_mount(0);
}

bool usb_fatfs_mount(uint8_t device) {
  if (!usb_fatfs_valid_device(device)) {
    Serial1.printf("*E: USB FatFs: invalid drive %u\n", (unsigned)device);
    return false;
  }

  if (gUSBFatFsMounted[device])
    return true;

  if (!usb_storage_device_mounted(device)) {
    Serial1.printf("*E: USB FatFs: no MSC device mounted for drive %u:\n", (unsigned)device);
    return false;
  }

  char drive[3];
  if (!usb_fatfs_drive_name(device, drive)) {
    Serial1.printf("*E: USB FatFs: cannot build drive name for %u\n", (unsigned)device);
    return false;
  }

  Serial1.printf("*I: USB FatFs: mounting drive %s\n", drive);
  FRESULT const fr = f_mount(&gUSBFatFs[device], drive, 1);
  if (fr != FR_OK) {
    Serial1.printf("*E: USB FatFs: f_mount %s failed fr=%u\n", drive, (unsigned)fr);
    return false;
  }

  gUSBFatFsMounted[device] = true;
  Serial1.printf("*I: USB FatFs mounted: drive %s\n", drive);
  return true;
}

void usb_fatfs_unmount() {
  usb_fatfs_unmount(0);
}

void usb_fatfs_unmount(uint8_t device) {
  if (!usb_fatfs_valid_device(device))
    return;

  if (!gUSBFatFsMounted[device])
    return;

  // Close only handles owned by the removed/unmounted device. Other mounted
  // USB sticks and their RP file handles remain valid.
  rp_fs_close_all_for_device(device);

  char drive[3];
  if (usb_fatfs_drive_name(device, drive))
    f_mount(nullptr, drive, 0);

  gUSBFatFsMounted[device] = false;
  Serial1.printf("*I: USB FatFs unmounted: drive %u:\n", (unsigned)device);
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
