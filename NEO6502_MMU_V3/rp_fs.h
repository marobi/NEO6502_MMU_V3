#pragma once

#include <Arduino.h>

// ============================================================
// rp_fs.h
// NEO MMU - RP-local filesystem handle API backed by FatFs
//
// Current milestone:
//   - USB MSC/FatFs is available through usb_storage/usb_fatfs
//   - read/write files through explicit open modes
//   - four RP-side handles
//   - each handle records its owning USB storage device/FatFs drive
//   - device-qualified FatFs paths accepted when they match the requested device
// ============================================================

static constexpr uint8_t RP_FS_MAX_HANDLES = 4;
static constexpr uint8_t RP_FS_MAX_DIR_HANDLES = 4;
static constexpr uint16_t RP_FS_DIRENT_SIZE = 18;

bool rp_fs_ready();
bool rp_fs_ready(uint8_t device);

// Opens a file and returns an RP-side handle 0..3.
// Returns -1 on failure. The name may be either "TEST.TXT" or "0:/TEST.TXT".
// The explicit-device overload maps "TEST.TXT" to "N:/TEST.TXT" and rejects
// an already-qualified path if its drive does not match the requested device.
// The _83 suffix is retained for the mailbox API shape, but FatFs performs the
// actual path/name parsing after RP-side validation.
int rp_fs_open_readonly_83(const char* filename);
int rp_fs_open_readonly_83(uint8_t device, const char* filename);
int rp_fs_open_write_truncate_83(const char* filename);
int rp_fs_open_write_truncate_83(uint8_t device, const char* filename);
int rp_fs_open_write_existing_83(const char* filename);
int rp_fs_open_write_existing_83(uint8_t device, const char* filename);
int rp_fs_open_rw_existing_83(const char* filename);
int rp_fs_open_rw_existing_83(uint8_t device, const char* filename);
int rp_fs_open_rw_create_83(const char* filename);
int rp_fs_open_rw_create_83(uint8_t device, const char* filename);

bool rp_fs_close(uint8_t handle);
int rp_fs_read(uint8_t handle, uint8_t* dst, uint16_t len);
int rp_fs_write(uint8_t handle, const uint8_t* src, uint16_t len);
bool rp_fs_seek(uint8_t handle, uint32_t position);
bool rp_fs_delete_83(const char* filename);
bool rp_fs_delete_83(uint8_t device, const char* filename);
bool rp_fs_rename_83(const char* old_filename, const char* new_filename);
bool rp_fs_rename_83(uint8_t device, const char* old_filename, const char* new_filename);
bool rp_fs_mkdir_83(const char* dirname);
bool rp_fs_mkdir_83(uint8_t device, const char* dirname);
bool rp_fs_rmdir_83(const char* dirname);
bool rp_fs_rmdir_83(uint8_t device, const char* dirname);

int rp_fs_opendir_83(const char* dirname);
int rp_fs_opendir_83(uint8_t device, const char* dirname);
int rp_fs_readdir(uint8_t handle, char* name, size_t name_len, uint8_t* attr, uint32_t* size);
bool rp_fs_closedir(uint8_t handle);
bool rp_fs_dir_is_open(uint8_t handle);
uint8_t rp_fs_free_dir_handle_count();

bool rp_fs_is_open(uint8_t handle);
uint8_t rp_fs_free_handle_count();

uint32_t rp_fs_size(uint8_t handle);
uint32_t rp_fs_position(uint8_t handle);

void rp_fs_close_all();
void rp_fs_close_all_for_device(uint8_t device);
uint8_t rp_fs_handle_device(uint8_t handle);

// Temporary RP-local validation only. Not a monitor command and not the 6502
// mailbox API. Remove or disable after the mailbox FS path is validated.
void rp_fs_test_read_files();
void rp_fs_test_read_files(uint8_t device);
