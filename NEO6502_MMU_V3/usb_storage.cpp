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
// V28c supports multiple detected MSC devices through fixed storage slots.
// FatFs physical drive N maps to storage slot N. The RP FS/mailbox open path
// can select the target device while legacy no-device calls still default to 0.
// ============================================================

#if defined(USE_TINYUSB_HOST)
  #include "Adafruit_TinyUSB.h"

  #ifndef CFG_TUH_HUB
  #define CFG_TUH_HUB 0
  #endif
  #ifndef CFG_TUH_DEVICE_MAX
  #define CFG_TUH_DEVICE_MAX 0
  #endif
  #ifndef CFG_TUH_MSC
  #define CFG_TUH_MSC 0
  #endif

  static Adafruit_USBH_Host gUSBHost;
#endif

struct USBStorageSlot {
  bool present;
  bool mount_pending;
  uint8_t dev_addr;
  uint8_t lun;
  uint32_t block_count;
  uint32_t block_size;
};

static bool gUSBStorageInitialized = false;
static USBStorageSlot gUSBStorageSlots[USB_STORAGE_MAX_DEVICES];

static volatile bool gReadDone = false;
static volatile bool gReadOK = false;

#if defined(USE_TINYUSB_HOST)
static bool usb_storage_msc_read_complete(uint8_t dev_addr, const tuh_msc_complete_data_t* cb_data);
#endif

static bool usb_storage_valid_device(uint8_t device) {
  return device < USB_STORAGE_MAX_DEVICES;
}

static USBStorageSlot* usb_storage_slot(uint8_t device) {
  if (!usb_storage_valid_device(device))
    return nullptr;
  return &gUSBStorageSlots[device];
}

static const USBStorageSlot* usb_storage_slot_const(uint8_t device) {
  if (!usb_storage_valid_device(device))
    return nullptr;
  return &gUSBStorageSlots[device];
}

static int usb_storage_find_slot_by_dev_addr(uint8_t dev_addr) {
  for (uint8_t i = 0; i < USB_STORAGE_MAX_DEVICES; i++) {
    if (gUSBStorageSlots[i].present && gUSBStorageSlots[i].dev_addr == dev_addr)
      return (int)i;
  }
  return -1;
}

static int usb_storage_find_free_slot() {
  for (uint8_t i = 0; i < USB_STORAGE_MAX_DEVICES; i++) {
    if (!gUSBStorageSlots[i].present)
      return (int)i;
  }
  return -1;
}

static void usb_storage_clear_slot(uint8_t device) {
  if (!usb_storage_valid_device(device))
    return;

  gUSBStorageSlots[device].present = false;
  gUSBStorageSlots[device].mount_pending = false;
  gUSBStorageSlots[device].dev_addr = 0;
  gUSBStorageSlots[device].lun = 0;
  gUSBStorageSlots[device].block_count = 0;
  gUSBStorageSlots[device].block_size = 0;
}

void initUSBStorage() {
  if (gUSBStorageInitialized)
    return;

#if defined(USE_TINYUSB_HOST)
  gUSBHost.begin(0);
#else
  Serial1.println("*E: USB host storage: USE_TINYUSB_HOST is not enabled");
#endif

  gUSBStorageInitialized = true;
}

/// <summary>
/// Service the TinyUSB host stack and perform RP-local FatFs mounts after USB
/// MSC devices are detected. Mounting stays in normal loop context; TinyUSB
/// callbacks only record device state.
/// </summary>
void taskUSBStorage() {
#if defined(USE_TINYUSB_HOST)
  if (!gUSBStorageInitialized)
    initUSBStorage();

  gUSBHost.task();

  for (uint8_t device = 0; device < USB_STORAGE_MAX_DEVICES; device++) {
    USBStorageSlot* const slot = usb_storage_slot(device);
    if (slot == nullptr || !slot->mount_pending)
      continue;

    slot->mount_pending = false;

    if (slot->present && !usb_fatfs_mounted(device)) {
      if (usb_fatfs_mount(device)) {
        Serial1.printf("*I: USB MSC ready: device=%u drive=%u:\n",
                       (unsigned)device,
                       (unsigned)device);
      }
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
  return usb_storage_device_mounted(0);
}

bool usb_storage_ready() {
  return usb_storage_ready(0);
}

bool usb_storage_device_mounted(uint8_t device) {
  const USBStorageSlot* const slot = usb_storage_slot_const(device);
  return slot != nullptr && slot->present;
}

bool usb_storage_ready(uint8_t device) {
  return usb_storage_device_mounted(device) && usb_fatfs_mounted(device);
}

uint8_t usb_storage_ready_mask() {
  uint8_t mask = 0;
  for (uint8_t device = 0; device < USB_STORAGE_MAX_DEVICES && device < 8; device++) {
    if (usb_storage_ready(device))
      mask |= (uint8_t)(1u << device);
  }
  return mask;
}

uint8_t usb_storage_mounted_count() {
  uint8_t count = 0;
  for (uint8_t device = 0; device < USB_STORAGE_MAX_DEVICES; device++) {
    if (usb_storage_device_mounted(device))
      count++;
  }
  return count;
}

uint8_t usb_storage_ready_count() {
  uint8_t count = 0;
  for (uint8_t device = 0; device < USB_STORAGE_MAX_DEVICES; device++) {
    if (usb_storage_ready(device))
      count++;
  }
  return count;
}

uint8_t usb_storage_device_address() { return usb_storage_device_address(0); }
uint8_t usb_storage_lun() { return usb_storage_lun(0); }
uint32_t usb_storage_block_count() { return usb_storage_block_count(0); }
uint32_t usb_storage_block_size() { return usb_storage_block_size(0); }

uint8_t usb_storage_device_address(uint8_t device) {
  const USBStorageSlot* const slot = usb_storage_slot_const(device);
  return slot != nullptr ? slot->dev_addr : 0;
}

uint8_t usb_storage_lun(uint8_t device) {
  const USBStorageSlot* const slot = usb_storage_slot_const(device);
  return slot != nullptr ? slot->lun : 0;
}

uint32_t usb_storage_block_count(uint8_t device) {
  const USBStorageSlot* const slot = usb_storage_slot_const(device);
  return slot != nullptr ? slot->block_count : 0;
}

uint32_t usb_storage_block_size(uint8_t device) {
  const USBStorageSlot* const slot = usb_storage_slot_const(device);
  return slot != nullptr ? slot->block_size : 0;
}

bool usb_storage_read_blocks(uint32_t lba, uint8_t* buffer, uint16_t count, uint32_t timeout_ms) {
  return usb_storage_read_blocks(0, lba, buffer, count, timeout_ms);
}

bool usb_storage_read_blocks(uint8_t device, uint32_t lba, uint8_t* buffer, uint16_t count, uint32_t timeout_ms) {
#if defined(USE_TINYUSB_HOST)
  if (!gUSBStorageInitialized)
    initUSBStorage();

  USBStorageSlot* const slot = usb_storage_slot(device);
  if (slot == nullptr) {
    Serial1.printf("*E: USB storage: invalid device %u\n", (unsigned)device);
    return false;
  }

  if (buffer == nullptr || count == 0)
    return false;

  if (!slot->present) {
    Serial1.printf("*E: USB storage: device %u not mounted\n", (unsigned)device);
    return false;
  }

  if (slot->block_size != 512) {
    Serial1.printf("*E: USB storage: device %u unsupported block size %lu\n",
                   (unsigned)device,
                   (unsigned long)slot->block_size);
    return false;
  }

  if (lba >= slot->block_count || (uint32_t)count > (slot->block_count - lba)) {
    Serial1.printf("*E: USB storage: device %u LBA range %lu+%u out of range\n",
                   (unsigned)device,
                   (unsigned long)lba,
                   count);
    return false;
  }

  if (!tuh_msc_ready(slot->dev_addr)) {
    Serial1.printf("*E: USB storage: device %u MSC busy/not ready\n", (unsigned)device);
    return false;
  }

  gReadDone = false;
  gReadOK = false;

  if (!tuh_msc_read10(slot->dev_addr,
                      slot->lun,
                      buffer,
                      lba,
                      count,
                      usb_storage_msc_read_complete,
                      (uintptr_t)lba)) {
    Serial1.printf("*E: USB storage: device %u failed to queue READ10\n", (unsigned)device);
    return false;
  }

  uint32_t const start_ms = millis();
  while (!gReadDone) {
    gUSBHost.task();
    if ((uint32_t)(millis() - start_ms) >= timeout_ms) {
      Serial1.printf("*E: USB storage: device %u READ10 timeout\n", (unsigned)device);
      return false;
    }
    delay(1);
  }

  if (!gReadOK) {
    Serial1.printf("*E: USB storage: device %u READ10 failed\n", (unsigned)device);
    return false;
  }

  return true;
#else
  (void)device;
  (void)lba;
  (void)buffer;
  (void)count;
  (void)timeout_ms;
  Serial1.println("*E: USB storage: USE_TINYUSB_HOST is not enabled");
  return false;
#endif
}

bool usb_storage_write_blocks(uint32_t lba, const uint8_t* buffer, uint16_t count, uint32_t timeout_ms) {
  return usb_storage_write_blocks(0, lba, buffer, count, timeout_ms);
}

bool usb_storage_write_blocks(uint8_t device, uint32_t lba, const uint8_t* buffer, uint16_t count, uint32_t timeout_ms) {
  (void)device;
  (void)lba;
  (void)buffer;
  (void)count;
  (void)timeout_ms;
  return false;
}

bool usb_storage_sync() {
  return usb_storage_sync(0);
}

bool usb_storage_sync(uint8_t device) {
  return usb_storage_device_mounted(device);
}

void usb_storage_print_summary() {
  Serial1.println("USB storage summary");
  Serial1.print("  host        : ");
  Serial1.println(usb_storage_host_enabled() ? "enabled" : "disabled");
  Serial1.print("  initialized : ");
  Serial1.println(gUSBStorageInitialized ? "yes" : "no");
  Serial1.printf("  devices     : mounted=%u ready=%u mask=%02X\n",
                 (unsigned)usb_storage_mounted_count(),
                 (unsigned)usb_storage_ready_count(),
                 (unsigned)usb_storage_ready_mask());
  Serial1.print("  drive 0 FatFs: ");
  Serial1.println(usb_fatfs_mounted(0) ? "mounted" : "not mounted");
  if (usb_storage_block_size(0) && usb_storage_block_count(0)) {
    uint64_t const bytes = (uint64_t)usb_storage_block_size(0) * (uint64_t)usb_storage_block_count(0);
    Serial1.printf("  block size  : %lu\n", (unsigned long)usb_storage_block_size(0));
    Serial1.printf("  block count : %lu\n", (unsigned long)usb_storage_block_count(0));
    Serial1.printf("  capacity    : %lu MiB\n", (unsigned long)(bytes / (1024ULL * 1024ULL)));
  }
}

void usb_storage_print_disks() {
  Serial1.println("USB disks");
  Serial1.println("slot default dev lun msc fatfs ready block_size block_count capacity_mib");

  for (uint8_t device = 0; device < USB_STORAGE_MAX_DEVICES; device++) {
    const USBStorageSlot* const slot = usb_storage_slot_const(device);
    if (slot == nullptr)
      continue;

    uint64_t const bytes = (uint64_t)slot->block_size * (uint64_t)slot->block_count;
    Serial1.printf("%4u %7s %3u %3u %3s %5s %5s %10lu %11lu %12lu\n",
                   (unsigned)device,
                   slot->present ? (device == 0 ? "yes" : "no") : "-",
                   (unsigned)slot->dev_addr,
                   (unsigned)slot->lun,
                   slot->present ? "yes" : "no",
                   usb_fatfs_mounted(device) ? "yes" : "no",
                   usb_storage_ready(device) ? "yes" : "no",
                   (unsigned long)slot->block_size,
                   (unsigned long)slot->block_count,
                   (unsigned long)(bytes / (1024ULL * 1024ULL)));
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

  int slot_index = usb_storage_find_slot_by_dev_addr(dev_addr);
  if (slot_index < 0)
    slot_index = usb_storage_find_free_slot();

  if (slot_index < 0) {
    Serial1.printf("*E: USB MSC mounted: dev=%u lun=%u but no free storage slot\n", dev_addr, lun);
    return;
  }

  uint8_t const device = (uint8_t)slot_index;
  USBStorageSlot* const slot = usb_storage_slot(device);
  if (slot == nullptr)
    return;

  slot->dev_addr = dev_addr;
  slot->lun = lun;
  slot->block_count = tuh_msc_get_block_count(dev_addr, lun);
  slot->block_size = tuh_msc_get_block_size(dev_addr, lun);
  slot->present = true;
  slot->mount_pending = true;

}

void tuh_msc_umount_cb(uint8_t dev_addr) {
  int const slot_index = usb_storage_find_slot_by_dev_addr(dev_addr);

  if (slot_index >= 0) {
    uint8_t const device = (uint8_t)slot_index;
    usb_fatfs_unmount(device);
    usb_storage_clear_slot(device);
    Serial1.printf("*I: USB MSC removed: slot=%u dev=%u\n", (unsigned)device, dev_addr);
    return;
  }

  Serial1.printf("*I: USB MSC removed: dev=%u unknown slot\n", dev_addr);
}
#endif
