#include <Arduino.h>
#include "rp_fs.h"
#include "usb_fatfs.h"

#include <ff.h>

// ============================================================
// rp_fs.cpp
// NEO MMU - RP-local filesystem handle API backed by FatFs
// ============================================================

using namespace fatfs;

struct RPFSHandle {
  bool in_use;
  FIL  file;
};

static RPFSHandle gRPFSHandles[RP_FS_MAX_HANDLES];

static bool rp_fs_valid_handle(uint8_t handle) {
  return handle < RP_FS_MAX_HANDLES && gRPFSHandles[handle].in_use;
}

static bool rp_fs_make_path(const char* filename, char* out, size_t out_len) {
  if (filename == nullptr || filename[0] == '\0' || out == nullptr || out_len == 0)
    return false;

  // Accept already-qualified FatFs paths such as "0:/TEST.TXT".
  if ((filename[0] >= '0' && filename[0] <= '9') && filename[1] == ':' && filename[2] == '/') {
    if (strlen(filename) >= out_len)
      return false;
    strcpy(out, filename);
    return true;
  }

  static constexpr char prefix[] = "0:/";
  size_t const prefix_len = sizeof(prefix) - 1;
  size_t const name_len = strlen(filename);
  if (prefix_len + name_len >= out_len)
    return false;

  memcpy(out, prefix, prefix_len);
  memcpy(out + prefix_len, filename, name_len + 1);
  return true;
}

bool rp_fs_ready() {
  return usb_fatfs_mounted();
}

int rp_fs_open_readonly_83(const char* filename) {
  if (!usb_fatfs_mounted()) {
    if (!usb_fatfs_mount())
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
  if (!rp_fs_make_path(filename, path, sizeof(path))) {
    Serial1.println("*E: RP FS: invalid or too long filename");
    return -1;
  }

  FRESULT const fr = f_open(&gRPFSHandles[slot].file, path, FA_READ);
  if (fr != FR_OK) {
    Serial1.printf("*W: RP FS: open %s failed fr=%u\n", path, (unsigned)fr);
    return -1;
  }

  gRPFSHandles[slot].in_use = true;
  return slot;
}

bool rp_fs_close(uint8_t handle) {
  if (!rp_fs_valid_handle(handle))
    return false;

  FRESULT const fr = f_close(&gRPFSHandles[handle].file);
  gRPFSHandles[handle].in_use = false;
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
    }
  }
}

static bool rp_fs_test_read_file(const char* filename, bool print_text_preview) {
  int const handle = rp_fs_open_readonly_83(filename);
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

  Serial1.printf("*I: RP FS: read %s size=%lu read=%lu chunks=%lu checksum=%08lX\n",
                 filename,
                 (unsigned long)size,
                 (unsigned long)total,
                 (unsigned long)chunks,
                 (unsigned long)checksum);

  return total == size;
}

void rp_fs_test_read_files() {
  if (!usb_fatfs_mounted()) {
    if (!usb_fatfs_mount())
      return;
  }

  rp_fs_test_read_file("TEST.TXT", true);
  rp_fs_test_read_file("BIG.TXT", false);
}
