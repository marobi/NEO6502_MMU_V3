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
  uint8_t flags;
  FIL  file;
};

struct RPFSDirHandle {
  bool in_use;
  uint8_t device;
  DIR dir;
};

static constexpr uint8_t RP_FS_HANDLE_READ  = 0x01;
static constexpr uint8_t RP_FS_HANDLE_WRITE = 0x02;

static RPFSHandle gRPFSHandles[RP_FS_MAX_HANDLES];
static RPFSDirHandle gRPFSDirHandles[RP_FS_MAX_DIR_HANDLES];

static bool rp_fs_valid_handle(uint8_t handle) {
  return handle < RP_FS_MAX_HANDLES && gRPFSHandles[handle].in_use;
}

static bool rp_fs_valid_dir_handle(uint8_t handle) {
  return handle < RP_FS_MAX_DIR_HANDLES && gRPFSDirHandles[handle].in_use;
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

bool rp_fs_dir_is_open(uint8_t handle) {
  return rp_fs_valid_dir_handle(handle);
}

uint8_t rp_fs_free_dir_handle_count() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < RP_FS_MAX_DIR_HANDLES; i++) {
    if (!gRPFSDirHandles[i].in_use)
      count++;
  }
  return count;
}

static bool rp_fs_valid_device(uint8_t device) {
  return device < USB_STORAGE_MAX_DEVICES;
}

static bool rp_fs_is_valid_83_name(const char* filename) {
  if (filename == nullptr || filename[0] == '\0')
    return false;

  const char* name = filename;
  if ((filename[0] >= '0' && filename[0] <= '9') && filename[1] == ':' && filename[2] == '/')
    name = filename + 3;

  uint8_t base_len = 0;
  uint8_t ext_len = 0;
  bool in_ext = false;

  for (const char* p = name; *p != '\0'; p++) {
    char const c = *p;

    if (c == '/' || c == '\\' || c == ':' || c < 33 || c >= 127)
      return false;

    if (c >= 'a' && c <= 'z')
      return false;

    if (c == '.') {
      if (in_ext || base_len == 0)
        return false;
      in_ext = true;
      continue;
    }

    if (!(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_' && c != '-' && c != '$' && c != '~')
      return false;

    if (in_ext) {
      ext_len++;
      if (ext_len > 3)
        return false;
    }
    else {
      base_len++;
      if (base_len > 8)
        return false;
    }
  }

  return base_len != 0 && (!in_ext || ext_len != 0);
}

static bool rp_fs_is_valid_83_path(const char* path);

static bool rp_fs_make_path(uint8_t device, const char* filename, char* out, size_t out_len) {
  if (filename == nullptr || filename[0] == '\0' || out == nullptr || out_len == 0)
    return false;

  if (!rp_fs_valid_device(device) || device > 9 || out_len < 4)
    return false;

  if (!rp_fs_is_valid_83_path(filename))
    return false;

  const char* logical = filename;
  if ((filename[0] >= '0' && filename[0] <= '9') && filename[1] == ':' && filename[2] == '/') {
    if ((uint8_t)(filename[0] - '0') != device)
      return false;
    logical = filename + 3;
  }

  // File operations require a file path, not a drive or directory root.
  if (logical[0] == '\0' || (logical[0] == '/' && logical[1] == '\0'))
    return false;

  // Accept already-qualified FatFs paths such as "0:/DIR/TEST.TXT" only when
  // the path drive matches the explicit RP FS device. This keeps handle device
  // ownership consistent with the mounted FatFs drive.
  if ((filename[0] >= '0' && filename[0] <= '9') && filename[1] == ':' && filename[2] == '/') {
    if (strlen(filename) >= out_len)
      return false;
    strcpy(out, filename);
    return true;
  }

  if (logical[0] == '/')
    logical++;

  size_t const name_len = strlen(logical);
  if (name_len == 0 || 3 + name_len >= out_len)
    return false;

  out[0] = (char)('0' + device);
  out[1] = ':';
  out[2] = '/';
  memcpy(out + 3, logical, name_len + 1);
  return true;
}

static bool rp_fs_is_valid_83_component(const char* start, size_t len) {
  if (start == nullptr || len == 0)
    return false;

  uint8_t base_len = 0;
  uint8_t ext_len = 0;
  bool in_ext = false;

  for (size_t i = 0; i < len; i++) {
    char const c = start[i];

    if (c == '\\' || c == ':' || c < 33 || c >= 127)
      return false;

    if (c >= 'a' && c <= 'z')
      return false;

    if (c == '.') {
      if (in_ext || base_len == 0)
        return false;
      in_ext = true;
      continue;
    }

    if (!(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_' && c != '-' && c != '$' && c != '~')
      return false;

    if (in_ext) {
      ext_len++;
      if (ext_len > 3)
        return false;
    }
    else {
      base_len++;
      if (base_len > 8)
        return false;
    }
  }

  return base_len != 0 && (!in_ext || ext_len != 0);
}

static bool rp_fs_is_valid_83_path(const char* path) {
  if (path == nullptr)
    return false;

  const char* name = path;
  if ((path[0] >= '0' && path[0] <= '9') && path[1] == ':' && path[2] == '/')
    name = path + 3;

  if (name[0] == '\0')
    return true;

  if (name[0] == '/') {
    name++;
    if (name[0] == '\0')
      return true;
  }

  const char* component = name;
  size_t component_len = 0;

  for (const char* p = name; ; p++) {
    char const c = *p;
    if (c == '/' || c == '\0') {
      if (!rp_fs_is_valid_83_component(component, component_len))
        return false;

      if (c == '\0')
        return true;

      component = p + 1;
      component_len = 0;
      if (*component == '\0')
        return false;
      continue;
    }

    component_len++;
  }
}

static bool rp_fs_make_dir_path(uint8_t device, const char* dirname, char* out, size_t out_len) {
  if (dirname == nullptr || out == nullptr || out_len == 0)
    return false;

  if (!rp_fs_valid_device(device) || device > 9 || out_len < 4)
    return false;

  if (!rp_fs_is_valid_83_path(dirname))
    return false;

  if ((dirname[0] >= '0' && dirname[0] <= '9') && dirname[1] == ':' && dirname[2] == '/') {
    if ((uint8_t)(dirname[0] - '0') != device)
      return false;
    if (strlen(dirname) >= out_len)
      return false;
    strcpy(out, dirname);
    return true;
  }

  out[0] = (char)('0' + device);
  out[1] = ':';
  out[2] = '/';

  if (dirname[0] == '\0' || (dirname[0] == '/' && dirname[1] == '\0')) {
    out[3] = '\0';
    return true;
  }

  const char* source = dirname[0] == '/' ? dirname + 1 : dirname;
  size_t const name_len = strlen(source);
  if (3 + name_len >= out_len)
    return false;

  memcpy(out + 3, source, name_len + 1);
  return true;
}

bool rp_fs_ready() {
  return usb_fatfs_mounted_mask() != 0;
}

bool rp_fs_ready(uint8_t device) {
  return rp_fs_valid_device(device) && usb_fatfs_mounted(device);
}

/// <summary>
/// rp_fs_open_mode_83 opens an 8.3 filesystem path on the selected USB/FatFs
/// device using the supplied FatFs mode and records the permitted NEOX handle
/// operations.
/// </summary>
/// <param name="device">RP USB storage device/FatFs drive number.</param>
/// <param name="filename">Unqualified 8.3 name or matching FatFs-qualified path.</param>
/// <param name="fatfs_mode">FatFs f_open mode flags.</param>
/// <param name="handle_flags">RP_FS_HANDLE_READ and/or RP_FS_HANDLE_WRITE.</param>
/// <param name="operation">Diagnostic operation text for Serial1 messages.</param>
/// <param name="invalid_name_message">Diagnostic text for invalid name failures.</param>
/// <returns>RP-side handle 0..RP_FS_MAX_HANDLES-1 on success, or -1 on failure.</returns>
static int rp_fs_open_mode_83(uint8_t device, const char* filename, uint8_t fatfs_mode, uint8_t handle_flags, const char* operation, const char* invalid_name_message) {
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
    Serial1.println(invalid_name_message);
    return -1;
  }

  FRESULT const fr = f_open(&gRPFSHandles[slot].file, path, fatfs_mode);
  if (fr != FR_OK) {
    Serial1.printf("*W: RP FS: %s %s failed fr=%u\n", operation, path, (unsigned)fr);
    return -1;
  }

  gRPFSHandles[slot].device = device;
  gRPFSHandles[slot].flags = handle_flags;
  gRPFSHandles[slot].in_use = true;
  return slot;
}

int rp_fs_open_readonly_83(const char* filename) {
  return rp_fs_open_readonly_83(0, filename);
}

int rp_fs_open_readonly_83(uint8_t device, const char* filename) {
  return rp_fs_open_mode_83(device, filename, FA_READ, RP_FS_HANDLE_READ, "open", "*E: RP FS: invalid or too long filename");
}

int rp_fs_open_write_truncate_83(const char* filename) {
  return rp_fs_open_write_truncate_83(0, filename);
}

int rp_fs_open_write_truncate_83(uint8_t device, const char* filename) {
  return rp_fs_open_mode_83(device, filename, FA_WRITE | FA_CREATE_ALWAYS, RP_FS_HANDLE_WRITE, "open write", "*E: RP FS: invalid 8.3 filename");
}

int rp_fs_open_write_existing_83(const char* filename) {
  return rp_fs_open_write_existing_83(0, filename);
}

int rp_fs_open_write_existing_83(uint8_t device, const char* filename) {
  return rp_fs_open_mode_83(device, filename, FA_READ | FA_WRITE | FA_OPEN_EXISTING, RP_FS_HANDLE_WRITE, "open write existing", "*E: RP FS: invalid 8.3 filename");
}

int rp_fs_open_rw_existing_83(const char* filename) {
  return rp_fs_open_rw_existing_83(0, filename);
}

int rp_fs_open_rw_existing_83(uint8_t device, const char* filename) {
  return rp_fs_open_mode_83(device, filename, FA_READ | FA_WRITE | FA_OPEN_EXISTING, RP_FS_HANDLE_READ | RP_FS_HANDLE_WRITE, "open rw existing", "*E: RP FS: invalid 8.3 filename");
}

int rp_fs_open_rw_create_83(const char* filename) {
  return rp_fs_open_rw_create_83(0, filename);
}

int rp_fs_open_rw_create_83(uint8_t device, const char* filename) {
  return rp_fs_open_mode_83(device, filename, FA_READ | FA_WRITE | FA_OPEN_ALWAYS, RP_FS_HANDLE_READ | RP_FS_HANDLE_WRITE, "open rw create", "*E: RP FS: invalid 8.3 filename");
}

bool rp_fs_close(uint8_t handle) {
  if (!rp_fs_valid_handle(handle))
    return false;

  FRESULT const fr = f_close(&gRPFSHandles[handle].file);
  gRPFSHandles[handle].in_use = false;
  gRPFSHandles[handle].device = 0;
  gRPFSHandles[handle].flags = 0;
  return fr == FR_OK;
}

int rp_fs_read(uint8_t handle, uint8_t* dst, uint16_t len) {
  if (!rp_fs_valid_handle(handle) || dst == nullptr)
    return -1;

  if ((gRPFSHandles[handle].flags & RP_FS_HANDLE_READ) == 0)
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

int rp_fs_write(uint8_t handle, const uint8_t* src, uint16_t len) {
  if (!rp_fs_valid_handle(handle) || src == nullptr)
    return -1;

  if ((gRPFSHandles[handle].flags & RP_FS_HANDLE_WRITE) == 0)
    return -1;

  if (len == 0)
    return 0;

  UINT bw = 0;
  FRESULT const fr = f_write(&gRPFSHandles[handle].file, src, len, &bw);
  if (fr != FR_OK) {
    Serial1.printf("*E: RP FS: write handle=%u failed fr=%u partial=%u\n", handle, (unsigned)fr, (unsigned)bw);
    return -1;
  }

  return (int)bw;
}

/// <summary>
/// rp_fs_seek moves an open RP filesystem handle to an absolute byte position.
/// Relative SEEK_SET/SEEK_CUR/SEEK_END arithmetic is owned by the mailbox layer.
/// </summary>
/// <param name="handle">Open RP-side filesystem handle.</param>
/// <param name="position">Absolute byte position from BOF.</param>
/// <returns>true when FatFs accepted the seek.</returns>
bool rp_fs_seek(uint8_t handle, uint32_t position) {
  if (!rp_fs_valid_handle(handle))
    return false;

  FRESULT const fr = f_lseek(&gRPFSHandles[handle].file, (FSIZE_t)position);
  if (fr != FR_OK) {
    Serial1.printf("*E: RP FS: seek handle=%u pos=%lu failed fr=%u\n", handle, (unsigned long)position, (unsigned)fr);
    return false;
  }

  return true;
}

/// <summary>
/// rp_fs_delete_83 deletes an 8.3 filesystem path on device 0.
/// </summary>
/// <param name="filename">Unqualified 8.3 name or matching FatFs-qualified path.</param>
/// <returns>true when FatFs deleted the path.</returns>
bool rp_fs_delete_83(const char* filename) {
  return rp_fs_delete_83(0, filename);
}

/// <summary>
/// rp_fs_delete_83 deletes an 8.3 filesystem path on the selected USB/FatFs
/// device. It uses the same path validation and device qualification rules as
/// file open.
/// </summary>
/// <param name="device">RP USB storage device/FatFs drive number.</param>
/// <param name="filename">Unqualified 8.3 name or matching FatFs-qualified path.</param>
/// <returns>true when FatFs deleted the path.</returns>
bool rp_fs_delete_83(uint8_t device, const char* filename) {
  if (!rp_fs_valid_device(device)) {
    Serial1.printf("*E: RP FS: delete invalid device %u\n", (unsigned)device);
    return false;
  }

  if (!usb_fatfs_mounted(device)) {
    if (!usb_fatfs_mount(device))
      return false;
  }

  char path[96];
  if (!rp_fs_make_path(device, filename, path, sizeof(path))) {
    Serial1.println("*E: RP FS: delete invalid 8.3 filename");
    return false;
  }

  FRESULT const fr = f_unlink(path);
  if (fr != FR_OK) {
    Serial1.printf("*W: RP FS: delete %s failed fr=%u\n", path, (unsigned)fr);
    return false;
  }

  return true;
}

/// <summary>
/// rp_fs_rename_83 renames an 8.3 filesystem path on device 0.
/// </summary>
/// <param name="old_filename">Existing unqualified 8.3 source name or matching FatFs-qualified path.</param>
/// <param name="new_filename">New unqualified 8.3 destination name or matching FatFs-qualified path.</param>
/// <returns>true when FatFs renamed the path.</returns>
bool rp_fs_rename_83(const char* old_filename, const char* new_filename) {
  return rp_fs_rename_83(0, old_filename, new_filename);
}

/// <summary>
/// rp_fs_rename_83 renames an 8.3 filesystem path on the selected USB/FatFs
/// device. Both paths must satisfy the same validation and explicit-device
/// qualification rules as file open.
/// </summary>
/// <param name="device">RP USB storage device/FatFs drive number.</param>
/// <param name="old_filename">Existing unqualified 8.3 source name or matching FatFs-qualified path.</param>
/// <param name="new_filename">New unqualified 8.3 destination name or matching FatFs-qualified path.</param>
/// <returns>true when FatFs renamed the path.</returns>
bool rp_fs_rename_83(uint8_t device, const char* old_filename, const char* new_filename) {
  if (!rp_fs_valid_device(device)) {
    Serial1.printf("*E: RP FS: rename invalid device %u\n", (unsigned)device);
    return false;
  }

  if (!usb_fatfs_mounted(device)) {
    if (!usb_fatfs_mount(device))
      return false;
  }

  char old_path[96];
  char new_path[96];
  if (!rp_fs_make_path(device, old_filename, old_path, sizeof(old_path)) ||
      !rp_fs_make_path(device, new_filename, new_path, sizeof(new_path))) {
    Serial1.println("*E: RP FS: rename invalid 8.3 filename");
    return false;
  }

  FRESULT const fr = f_rename(old_path, new_path);
  if (fr != FR_OK) {
    Serial1.printf("*W: RP FS: rename %s -> %s failed fr=%u\n", old_path, new_path, (unsigned)fr);
    return false;
  }

  return true;
}

/// <summary>
/// rp_fs_is_root_dir_path checks whether a fully-qualified FatFs directory path
/// identifies a drive root. V37 mkdir/rmdir deliberately rejects roots.
/// </summary>
/// <param name="path">FatFs path produced by rp_fs_make_dir_path.</param>
/// <returns>true when the path is a root such as "0:/".</returns>
static bool rp_fs_is_root_dir_path(const char* path) {
  if (path == nullptr)
    return false;

  return path[0] >= '0' && path[0] <= '9' && path[1] == ':' && path[2] == '/' && path[3] == '\0';
}

/// <summary>
/// rp_fs_mkdir_83 creates an 8.3 directory path on device 0.
/// </summary>
/// <param name="dirname">Unqualified 8.3 path or matching FatFs-qualified path.</param>
/// <returns>true when FatFs created the directory.</returns>
bool rp_fs_mkdir_83(const char* dirname) {
  return rp_fs_mkdir_83(0, dirname);
}

/// <summary>
/// rp_fs_mkdir_83 creates an 8.3 directory path on the selected USB/FatFs
/// device. Parent directories must already exist. Directory roots are rejected.
/// </summary>
/// <param name="device">RP USB storage device/FatFs drive number.</param>
/// <param name="dirname">Unqualified 8.3 path or matching FatFs-qualified path.</param>
/// <returns>true when FatFs created the directory.</returns>
bool rp_fs_mkdir_83(uint8_t device, const char* dirname) {
  if (!rp_fs_valid_device(device)) {
    Serial1.printf("*E: RP FS: mkdir invalid device %u\n", (unsigned)device);
    return false;
  }

  if (!usb_fatfs_mounted(device)) {
    if (!usb_fatfs_mount(device))
      return false;
  }

  char path[96];
  if (!rp_fs_make_dir_path(device, dirname, path, sizeof(path)) || rp_fs_is_root_dir_path(path)) {
    Serial1.println("*E: RP FS: mkdir invalid 8.3 directory path");
    return false;
  }

  FRESULT const fr = f_mkdir(path);
  if (fr != FR_OK) {
    Serial1.printf("*W: RP FS: mkdir %s failed fr=%u\n", path, (unsigned)fr);
    return false;
  }

  return true;
}

/// <summary>
/// rp_fs_rmdir_83 removes an empty 8.3 directory path on device 0.
/// </summary>
/// <param name="dirname">Unqualified 8.3 path or matching FatFs-qualified path.</param>
/// <returns>true when FatFs removed the directory.</returns>
bool rp_fs_rmdir_83(const char* dirname) {
  return rp_fs_rmdir_83(0, dirname);
}

/// <summary>
/// rp_fs_rmdir_83 removes an empty 8.3 directory path on the selected USB/FatFs
/// device. FatFs rejects non-empty directories. Directory roots are rejected.
/// </summary>
/// <param name="device">RP USB storage device/FatFs drive number.</param>
/// <param name="dirname">Unqualified 8.3 path or matching FatFs-qualified path.</param>
/// <returns>true when FatFs removed the directory.</returns>
bool rp_fs_rmdir_83(uint8_t device, const char* dirname) {
  if (!rp_fs_valid_device(device)) {
    Serial1.printf("*E: RP FS: rmdir invalid device %u\n", (unsigned)device);
    return false;
  }

  if (!usb_fatfs_mounted(device)) {
    if (!usb_fatfs_mount(device))
      return false;
  }

  char path[96];
  if (!rp_fs_make_dir_path(device, dirname, path, sizeof(path)) || rp_fs_is_root_dir_path(path)) {
    Serial1.println("*E: RP FS: rmdir invalid 8.3 directory path");
    return false;
  }

  FRESULT const fr = f_unlink(path);
  if (fr != FR_OK) {
    Serial1.printf("*W: RP FS: rmdir %s failed fr=%u\n", path, (unsigned)fr);
    return false;
  }

  return true;
}

/// <summary>
/// rp_fs_opendir_83 opens an 8.3 directory path on device 0.
/// </summary>
/// <param name="dirname">Unqualified 8.3 path, root path, or matching FatFs-qualified path.</param>
/// <returns>RP-side directory handle 0..RP_FS_MAX_DIR_HANDLES-1 on success, or -1 on failure.</returns>
int rp_fs_opendir_83(const char* dirname) {
  return rp_fs_opendir_83(0, dirname);
}

/// <summary>
/// rp_fs_opendir_83 opens an explicit 8.3 directory path on the selected
/// USB/FatFs device. This does not use or modify a global current directory.
/// </summary>
/// <param name="device">RP USB storage device/FatFs drive number.</param>
/// <param name="dirname">Unqualified 8.3 path, root path, or matching FatFs-qualified path.</param>
/// <returns>RP-side directory handle 0..RP_FS_MAX_DIR_HANDLES-1 on success, or -1 on failure.</returns>
int rp_fs_opendir_83(uint8_t device, const char* dirname) {
  if (!rp_fs_valid_device(device)) {
    Serial1.printf("*E: RP FS: opendir invalid device %u\n", (unsigned)device);
    return -1;
  }

  if (!usb_fatfs_mounted(device)) {
    if (!usb_fatfs_mount(device))
      return -1;
  }

  int slot = -1;
  for (uint8_t i = 0; i < RP_FS_MAX_DIR_HANDLES; i++) {
    if (!gRPFSDirHandles[i].in_use) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    Serial1.println("*E: RP FS: no free dir handles");
    return -1;
  }

  char path[96];
  if (!rp_fs_make_dir_path(device, dirname, path, sizeof(path))) {
    Serial1.println("*E: RP FS: opendir invalid 8.3 path");
    return -1;
  }

  FRESULT const fr = f_opendir(&gRPFSDirHandles[slot].dir, path);
  if (fr != FR_OK) {
    Serial1.printf("*W: RP FS: opendir %s failed fr=%u\n", path, (unsigned)fr);
    return -1;
  }

  gRPFSDirHandles[slot].device = device;
  gRPFSDirHandles[slot].in_use = true;
  return slot;
}

/// <summary>
/// rp_fs_readdir reads the next entry from an open RP directory handle.
/// Dot entries are skipped. The return value distinguishes EOF from error.
/// </summary>
/// <param name="handle">Open RP-side directory handle.</param>
/// <param name="name">Destination for the NUL-terminated 8.3 entry name.</param>
/// <param name="name_len">Size of the name destination buffer.</param>
/// <param name="attr">Destination for FatFs attribute flags.</param>
/// <param name="size">Destination for file size; directories report their FatFs size value.</param>
/// <returns>1 when an entry was returned, 0 at end of directory, or -1 on error.</returns>
int rp_fs_readdir(uint8_t handle, char* name, size_t name_len, uint8_t* attr, uint32_t* size) {
  if (!rp_fs_valid_dir_handle(handle) || name == nullptr || name_len < 13 || attr == nullptr || size == nullptr)
    return -1;

  FILINFO info{};
  for (;;) {
    FRESULT const fr = f_readdir(&gRPFSDirHandles[handle].dir, &info);
    if (fr != FR_OK) {
      Serial1.printf("*E: RP FS: readdir handle=%u failed fr=%u\n", handle, (unsigned)fr);
      return -1;
    }

    if (info.fname[0] == '\0') {
      name[0] = '\0';
      *attr = 0;
      *size = 0;
      return 0;
    }

    if (strcmp(info.fname, ".") == 0 || strcmp(info.fname, "..") == 0)
      continue;

    const char* entry_name = info.fname;
#if FF_USE_LFN
    if (!rp_fs_is_valid_83_name(entry_name)) {
      if (info.altname[0] != '\0' && rp_fs_is_valid_83_name(info.altname))
        entry_name = info.altname;
      else
        continue;
    }
#else
    if (!rp_fs_is_valid_83_name(entry_name))
      continue;
#endif

    size_t const len = strlen(entry_name);
    if (len >= name_len) {
      Serial1.printf("*E: RP FS: readdir 8.3 name too long: %s\n", entry_name);
      return -1;
    }

    memcpy(name, entry_name, len + 1);
    *attr = (uint8_t)info.fattrib;
    *size = (uint32_t)info.fsize;
    return 1;
  }
}

/// <summary>
/// rp_fs_closedir closes an open RP directory handle and releases the slot.
/// </summary>
/// <param name="handle">Open RP-side directory handle.</param>
/// <returns>true when FatFs accepted the close operation.</returns>
bool rp_fs_closedir(uint8_t handle) {
  if (!rp_fs_valid_dir_handle(handle))
    return false;

  FRESULT const fr = f_closedir(&gRPFSDirHandles[handle].dir);
  gRPFSDirHandles[handle].in_use = false;
  gRPFSDirHandles[handle].device = 0;
  return fr == FR_OK;
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
      gRPFSHandles[i].flags = 0;
    }
  }

  for (uint8_t i = 0; i < RP_FS_MAX_DIR_HANDLES; i++) {
    if (gRPFSDirHandles[i].in_use) {
      f_closedir(&gRPFSDirHandles[i].dir);
      gRPFSDirHandles[i].in_use = false;
      gRPFSDirHandles[i].device = 0;
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
      gRPFSHandles[i].flags = 0;
    }
  }

  for (uint8_t i = 0; i < RP_FS_MAX_DIR_HANDLES; i++) {
    if (gRPFSDirHandles[i].in_use && gRPFSDirHandles[i].device == device) {
      f_closedir(&gRPFSDirHandles[i].dir);
      gRPFSDirHandles[i].in_use = false;
      gRPFSDirHandles[i].device = 0;
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
