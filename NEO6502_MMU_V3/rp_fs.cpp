#include <Arduino.h>
#include "rp_fs.h"
#include "usb_fatfs.h"
#include "usb_storage.h"

#include "fatfs_local/ff.h"

// ============================================================
// rp_fs.cpp
// NEO MMU - RP-local filesystem handle API backed by FatFs
// ============================================================

using namespace fatfs;

struct RPFSHandle {
  bool in_use;
  uint8_t device;
  FIL  file;
};

static RPFSHandle gRPFSHandles[RP_FS_MAX_HANDLES];

static bool rp_fs_valid_handle(uint8_t handle) {
  return handle < RP_FS_MAX_HANDLES && gRPFSHandles[handle].in_use;
}

bool rp_fs_is_open(uint8_t handle) {
  return rp_fs_valid_handle(handle);
}

uint8_t rp_fs_free_handle_count() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < RP_FS_MAX_HANDLES; i++) {
    if (!gRPFSHandles[i].in_use)
      count++;
  }
  return count;
}

static bool rp_fs_valid_device(uint8_t device) {
  return device < USB_STORAGE_MAX_DEVICES;
}

static bool rp_fs_make_path(uint8_t device, const char* filename, char* out, size_t out_len) {
  if (filename == nullptr || filename[0] == '\0' || out == nullptr || out_len == 0)
    return false;

  // Accept already-qualified FatFs paths such as "0:/TEST.TXT" only when
  // the path drive matches the explicit RP FS device. This keeps handle device
  // ownership consistent with the mounted FatFs drive.
  if ((filename[0] >= '0' && filename[0] <= '9') && filename[1] == ':' && filename[2] == '/') {
    if ((uint8_t)(filename[0] - '0') != device)
      return false;
    if (strlen(filename) >= out_len)
      return false;
    strcpy(out, filename);
    return true;
  }

  if (!rp_fs_valid_device(device) || device > 9 || out_len < 4)
    return false;

  size_t const name_len = strlen(filename);
  if (3 + name_len >= out_len)
    return false;

  out[0] = (char)('0' + device);
  out[1] = ':';
  out[2] = '/';
  memcpy(out + 3, filename, name_len + 1);
  return true;
}

bool rp_fs_ready() {
  return usb_fatfs_mounted_mask() != 0;
}

bool rp_fs_ready(uint8_t device) {
  return rp_fs_valid_device(device) && usb_fatfs_mounted(device);
}

int rp_fs_open_readonly_83(const char* filename) {
  return rp_fs_open_readonly_83(0, filename);
}

int rp_fs_open_readonly_83(uint8_t device, const char* filename) {
  if (!rp_fs_valid_device(device)) {
    Serial1.printf("*E: RP FS: invalid device %u\n", (unsigned)device);
    return -1;
  }

  if (!usb_fatfs_mounted(device)) {
    if (!usb_fatfs_mount(device))
      return -1;
  }

  int slot = -1;
  for (uint8_t i = 0; i < RP_FS_MAX_HANDLES; i++) {
    if (!gRPFSHandles[i].in_use) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    Serial1.println("*E: RP FS: no free file handles");
    return -1;
  }

  char path[96];
  if (!rp_fs_make_path(device, filename, path, sizeof(path))) {
    Serial1.println("*E: RP FS: invalid or too long filename");
    return -1;
  }

  FRESULT const fr = f_open(&gRPFSHandles[slot].file, path, FA_READ);
  if (fr != FR_OK) {
    Serial1.printf("*W: RP FS: open %s failed fr=%u\n", path, (unsigned)fr);
    return -1;
  }

  gRPFSHandles[slot].device = device;
  gRPFSHandles[slot].in_use = true;
  return slot;
}

bool rp_fs_close(uint8_t handle) {
  if (!rp_fs_valid_handle(handle))
    return false;

  FRESULT const fr = f_close(&gRPFSHandles[handle].file);
  gRPFSHandles[handle].in_use = false;
  gRPFSHandles[handle].device = 0;
  return fr == FR_OK;
}

int rp_fs_read(uint8_t handle, uint8_t* dst, uint16_t len) {
  if (!rp_fs_valid_handle(handle) || dst == nullptr)
    return -1;

  if (len == 0)
    return 0;

  UINT br = 0;
  FRESULT const fr = f_read(&gRPFSHandles[handle].file, dst, len, &br);
  if (fr != FR_OK) {
    Serial1.printf("*E: RP FS: read handle=%u failed fr=%u\n", handle, (unsigned)fr);
    return -1;
  }

  return (int)br;
}

uint32_t rp_fs_size(uint8_t handle) {
  if (!rp_fs_valid_handle(handle))
    return 0;

  return (uint32_t)f_size(&gRPFSHandles[handle].file);
}

uint32_t rp_fs_position(uint8_t handle) {
  if (!rp_fs_valid_handle(handle))
    return 0;

  return (uint32_t)f_tell(&gRPFSHandles[handle].file);
}

void rp_fs_close_all() {
  for (uint8_t i = 0; i < RP_FS_MAX_HANDLES; i++) {
    if (gRPFSHandles[i].in_use) {
      f_close(&gRPFSHandles[i].file);
      gRPFSHandles[i].in_use = false;
      gRPFSHandles[i].device = 0;
    }
  }
}

void rp_fs_close_all_for_device(uint8_t device) {
  if (!rp_fs_valid_device(device))
    return;

  for (uint8_t i = 0; i < RP_FS_MAX_HANDLES; i++) {
    if (gRPFSHandles[i].in_use && gRPFSHandles[i].device == device) {
      f_close(&gRPFSHandles[i].file);
      gRPFSHandles[i].in_use = false;
      gRPFSHandles[i].device = 0;
    }
  }
}

uint8_t rp_fs_handle_device(uint8_t handle) {
  if (!rp_fs_valid_handle(handle))
    return 0xFF;
  return gRPFSHandles[handle].device;
}

static bool rp_fs_test_read_file(uint8_t device, const char* filename, bool print_text_preview) {
  int const handle = rp_fs_open_readonly_83(device, filename);
  if (handle < 0)
    return false;

  uint8_t buffer[256];
  uint32_t total = 0;
  uint32_t checksum = 0;
  uint32_t chunks = 0;
  bool first_preview = print_text_preview;

  for (;;) {
    int const n = rp_fs_read((uint8_t)handle, buffer, sizeof(buffer));
    if (n < 0) {
      rp_fs_close((uint8_t)handle);
      return false;
    }

    if (n == 0)
      break;

    total += (uint32_t)n;
    chunks++;
    for (int i = 0; i < n; i++)
      checksum = (checksum + buffer[i]) & 0xFFFFFFFFu;

    if (first_preview) {
      Serial1.print("    text: ");
      for (int i = 0; i < n && i < 80; i++) {
        uint8_t const c = buffer[i];
        Serial1.write((c >= 32 && c < 127) ? c : '.');
      }
      Serial1.println();
      first_preview = false;
    }
  }

  uint32_t const size = rp_fs_size((uint8_t)handle);
  rp_fs_close((uint8_t)handle);

  Serial1.printf("*I: RP FS: dev=%u read %s size=%lu read=%lu chunks=%lu checksum=%08lX\n",
                 (unsigned)device,
                 filename,
                 (unsigned long)size,
                 (unsigned long)total,
                 (unsigned long)chunks,
                 (unsigned long)checksum);

  return total == size;
}

void rp_fs_test_read_files() {
  rp_fs_test_read_files(0);
}

void rp_fs_test_read_files(uint8_t device) {
  if (!rp_fs_valid_device(device)) {
    Serial1.printf("*E: RP FS: invalid test device %u\n", (unsigned)device);
    return;
  }

  if (!usb_fatfs_mounted(device)) {
    if (!usb_fatfs_mount(device))
      return;
  }

  rp_fs_test_read_file(device, "TEST.TXT", true);
  rp_fs_test_read_file(device, "BIG.TXT", false);
}
