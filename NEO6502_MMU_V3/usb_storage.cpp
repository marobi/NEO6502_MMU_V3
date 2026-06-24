#include <Arduino.h>
#include "usb_storage.h"
#include "usb_fatfs.h"
#include "rp_fs.h"

// ============================================================
// usb_storage.cpp
// NEO MMU - RP2350 USB host storage service
//
// Uses Adafruit TinyUSB host mode for USB MSC block access. Filesystem parsing
// is handled by FatFs through usb_msc_diskio.cpp; this module deliberately no
// longer contains a private FAT32/MBR parser.
//
// 6502 mailbox filesystem commands are added later after this RP-local layer is
// stable.
// ============================================================

#if defined(USE_TINYUSB_HOST)
  #include "Adafruit_TinyUSB.h"

  static Adafruit_USBH_Host gUSBHost;
#endif

static bool     gUSBStorageInitialized = false;
static bool     gUSBStorageDeviceMounted = false;
static bool     gUSBStorageMountPending = false;
static uint8_t  gUSBStorageDevAddr = 0;
static uint8_t  gUSBStorageLun = 0;
static uint32_t gUSBStorageBlockCount = 0;
static uint32_t gUSBStorageBlockSize = 0;

static volatile bool gReadDone = false;
static volatile bool gReadOK = false;

#if defined(USE_TINYUSB_HOST)
static bool usb_storage_msc_read_complete(uint8_t dev_addr, const tuh_msc_complete_data_t* cb_data);
#endif

void initUSBStorage() {
  if (gUSBStorageInitialized)
    return;

#if defined(USE_TINYUSB_HOST)
  Serial1.println("*I: USB host storage: init native host controller");
  gUSBHost.begin(0);
#else
  Serial1.println("*E: USB host storage: USE_TINYUSB_HOST is not enabled");
#endif

  gUSBStorageInitialized = true;
}

/// <summary>
/// Service the TinyUSB host stack and perform the RP-local FatFs mount test
/// after a USB MSC device is detected.
/// </summary>
void taskUSBStorage() {
#if defined(USE_TINYUSB_HOST)
  if (!gUSBStorageInitialized)
    initUSBStorage();

  gUSBHost.task();

  if (gUSBStorageMountPending) {
    gUSBStorageMountPending = false;

    // The TinyUSB callback only records device presence. Normal task context
    // performs the synchronous FatFs mount/open/read validation.
    if (gUSBStorageDeviceMounted && !usb_fatfs_mounted()) {
      if (usb_fatfs_mount())
        rp_fs_test_read_files();
    }
  }
#endif
}

bool usb_storage_host_enabled() {
#if defined(USE_TINYUSB_HOST)
  return true;
#else
  return false;
#endif
}

bool usb_storage_device_mounted() {
  return gUSBStorageDeviceMounted;
}

bool usb_storage_ready() {
  return gUSBStorageDeviceMounted && usb_fatfs_mounted();
}

uint8_t usb_storage_device_address() { return gUSBStorageDevAddr; }
uint8_t usb_storage_lun() { return gUSBStorageLun; }
uint32_t usb_storage_block_count() { return gUSBStorageBlockCount; }
uint32_t usb_storage_block_size() { return gUSBStorageBlockSize; }

bool usb_storage_read_blocks(uint32_t lba, uint8_t* buffer, uint16_t count, uint32_t timeout_ms) {
#if defined(USE_TINYUSB_HOST)
  if (!gUSBStorageInitialized)
    initUSBStorage();

  if (buffer == nullptr || count == 0)
    return false;

  if (!gUSBStorageDeviceMounted) {
    Serial1.println("*E: USB storage: no MSC device mounted");
    return false;
  }

  if (gUSBStorageBlockSize != 512) {
    Serial1.printf("*E: USB storage: unsupported block size %lu\n", (unsigned long)gUSBStorageBlockSize);
    return false;
  }

  if (lba >= gUSBStorageBlockCount || (uint32_t)count > (gUSBStorageBlockCount - lba)) {
    Serial1.printf("*E: USB storage: LBA range %lu+%u out of range\n", (unsigned long)lba, count);
    return false;
  }

  if (!tuh_msc_ready(gUSBStorageDevAddr)) {
    Serial1.println("*E: USB storage: MSC device busy/not ready");
    return false;
  }

  gReadDone = false;
  gReadOK = false;

  if (!tuh_msc_read10(gUSBStorageDevAddr,
                      gUSBStorageLun,
                      buffer,
                      lba,
                      count,
                      usb_storage_msc_read_complete,
                      (uintptr_t)lba)) {
    Serial1.println("*E: USB storage: failed to queue READ10");
    return false;
  }

  uint32_t const start_ms = millis();
  while (!gReadDone) {
    gUSBHost.task();
    if ((uint32_t)(millis() - start_ms) >= timeout_ms) {
      Serial1.println("*E: USB storage: READ10 timeout");
      return false;
    }
    delay(1);
  }

  if (!gReadOK) {
    Serial1.println("*E: USB storage: READ10 failed");
    return false;
  }

  return true;
#else
  (void)lba;
  (void)buffer;
  (void)count;
  (void)timeout_ms;
  Serial1.println("*E: USB storage: USE_TINYUSB_HOST is not enabled");
  return false;
#endif
}

bool usb_storage_write_blocks(uint32_t lba, const uint8_t* buffer, uint16_t count, uint32_t timeout_ms) {
  (void)lba;
  (void)buffer;
  (void)count;
  (void)timeout_ms;
  return false;
}

bool usb_storage_sync() {
  return gUSBStorageDeviceMounted;
}

void usb_storage_print_summary() {
  Serial1.println("USB storage summary");
  Serial1.print("  host        : ");
  Serial1.println(usb_storage_host_enabled() ? "enabled" : "disabled");
  Serial1.print("  initialized : ");
  Serial1.println(gUSBStorageInitialized ? "yes" : "no");
  Serial1.print("  MSC device  : ");
  Serial1.println(gUSBStorageDeviceMounted ? "mounted" : "not mounted");
  Serial1.print("  FatFs       : ");
  Serial1.println(usb_fatfs_mounted() ? "mounted" : "not mounted");
  if (gUSBStorageBlockSize && gUSBStorageBlockCount) {
    uint64_t const bytes = (uint64_t)gUSBStorageBlockSize * (uint64_t)gUSBStorageBlockCount;
    Serial1.printf("  block size  : %lu\n", (unsigned long)gUSBStorageBlockSize);
    Serial1.printf("  block count : %lu\n", (unsigned long)gUSBStorageBlockCount);
    Serial1.printf("  capacity    : %lu MiB\n", (unsigned long)(bytes / (1024ULL * 1024ULL)));
  }
}

#if defined(USE_TINYUSB_HOST)
static bool usb_storage_msc_read_complete(uint8_t dev_addr, const tuh_msc_complete_data_t* cb_data) {
  (void)dev_addr;

  gReadOK = (cb_data != nullptr) &&
            (cb_data->csw != nullptr) &&
            (cb_data->csw->status == 0);
  gReadDone = true;
  return true;
}

void tuh_msc_mount_cb(uint8_t dev_addr) {
  uint8_t const lun = 0;

  gUSBStorageDevAddr = dev_addr;
  gUSBStorageLun = lun;
  gUSBStorageBlockCount = tuh_msc_get_block_count(dev_addr, lun);
  gUSBStorageBlockSize = tuh_msc_get_block_size(dev_addr, lun);
  gUSBStorageDeviceMounted = true;
  gUSBStorageMountPending = true;

  Serial1.printf("*I: USB MSC mounted: dev=%u lun=%u\n", dev_addr, lun);
  Serial1.printf("    block size        : %lu\n", (unsigned long)gUSBStorageBlockSize);
  Serial1.printf("    block count       : %lu\n", (unsigned long)gUSBStorageBlockCount);
}

void tuh_msc_umount_cb(uint8_t dev_addr) {
  if (dev_addr == gUSBStorageDevAddr) {
    usb_fatfs_unmount();
    gUSBStorageDeviceMounted = false;
    gUSBStorageMountPending = false;
    gUSBStorageDevAddr = 0;
    gUSBStorageLun = 0;
    gUSBStorageBlockCount = 0;
    gUSBStorageBlockSize = 0;
  }

  Serial1.printf("*I: USB MSC removed: dev=%u\n", dev_addr);
}
#endif
