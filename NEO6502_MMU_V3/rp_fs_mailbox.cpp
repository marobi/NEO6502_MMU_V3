#include <Arduino.h>
#include <stdint.h>
#include <string.h>

#include "mailbox.h"
#include "memory_config.h"
#include "mmu.h"
#include "neobus.h"
#include "p6502.h"
#include "rp_fs.h"
#include "usb_fatfs.h"

// ============================================================
// rp_fs_mailbox.cpp
// NEO MMU - filesystem mailbox command bridge
//
// Filesystem command semantics only. mailbox.cpp owns the single central
// mailbox command table and dispatch.
//
// Command protocol, using mailbox ABI v2 request/result fields:
//
//   FS_STATUS:
//     in : none
//     out: RES0 bit 0      = at least one filesystem ready/mounted
//          RES0 bits 8..15 = number of mounted FatFs devices
//          FLAGS bits 0..7 = mounted/ready device bitmask
//
//   FS_OPEN:
//     in : ARG0 = pointer to NUL-terminated 8.3 filename in 6502 RAM
//          ARG1 = maximum filename bytes to scan, including NUL
//          ARG2 low byte  = open flags: 0=read existing, 1=write create/truncate
//          ARG2 high byte = device id / FatFs drive number
//     out: RES0 = RP file handle 0..RP_FS_MAX_HANDLES-1
//
//   FS_READ:
//     in : ARG0 = destination pointer in 6502 RAM
//          ARG1 = requested byte count; capped to 256 bytes per command
//          ARG2 = RP file handle
//     out: RES0 = bytes copied to destination
//          FLAGS bit 0 = EOF after this read
//
//   FS_WRITE:
//     in : ARG0 = source pointer in 6502 RAM
//          ARG1 = requested byte count; capped to 256 bytes per command
//          ARG2 = RP file handle
//     out: RES0 = bytes written
//
//   FS_CLOSE:
//     in : ARG2 = RP file handle
//     out: RES0 = 0
//
//   FS_LOAD:
//     in : ARG0 = pointer to fs_load_args in caller context
//          ARG1 = sizeof(fs_load_args), currently 8
//          ARG2 low byte  = trusted caller context supplied by NEOX kernel
//          ARG2 high byte = 0
//     out: RES0 = bytes loaded; 0 means 65536 bytes for a full-64K transfer
//
//   FS_SAVE:
//     in : ARG0 = pointer to fs_save_args in caller context
//          ARG1 = sizeof(fs_save_args), currently 8
//          ARG2 low byte  = trusted caller context supplied by NEOX kernel
//          ARG2 high byte = 0
//     out: RES0 = bytes saved; 0 means 65536 bytes for a full-64K transfer
//
//   FS_SEEK:
//     in : ARG0 = signed 32-bit offset low word
//          ARG1 = signed 32-bit offset high word
//          ARG2 low byte  = RP file handle
//          ARG2 high byte = whence: SEEK_SET, SEEK_CUR, SEEK_END
//     out: RES0/RES1 = resulting unsigned 32-bit file position
//
//   FS_TELL:
//     in : ARG2 low byte = RP file handle
//     out: RES0/RES1 = current unsigned 32-bit file position
//
//   FS_DELETE:
//     in : ARG0 = pointer to NUL-terminated 8.3 filename in 6502 RAM
//          ARG1 = maximum filename bytes to scan, including NUL
//          ARG2 low byte  = device id / FatFs drive number
//          ARG2 high byte = 0
//     out: RES0 = 0
//
//   FS_RENAME:
//     in : ARG0 = pointer to old NUL-terminated 8.3 filename in 6502 RAM
//          ARG1 = pointer to new NUL-terminated 8.3 filename in 6502 RAM
//          ARG2 low byte  = maximum filename bytes to scan, including NUL
//          ARG2 high byte = device id / FatFs drive number
//     out: RES0 = 0
//
//   FS_OPENDIR:
//     in : ARG0 = pointer to NUL-terminated explicit 8.3 directory path
//          ARG1 = maximum path bytes to scan, including NUL
//          ARG2 low byte  = device id / FatFs drive number
//          ARG2 high byte = 0
//     out: RES0 = RP directory handle 0..RP_FS_MAX_DIR_HANDLES-1
//
//   FS_READDIR:
//     in : ARG0 = destination dir_entry buffer in 6502 RAM
//          ARG1 = destination buffer size, must be at least RP_FS_DIRENT_SIZE
//          ARG2 low byte  = RP directory handle
//          ARG2 high byte = 0
//     out: RES0 = 1 when an entry was returned, 0 at end of directory
//          FLAGS bit 0 = EOF / no more directory entries
//
//   FS_CLOSEDIR:
//     in : ARG2 low byte  = RP directory handle
//          ARG2 high byte = 0
//     out: RES0 = 0
//
// Directory commands use explicit paths only. The RP side does not maintain
// a global current directory; NEOX resolves relative paths before calling RP.
// Long-filename commands are not implemented in this milestone.
// ============================================================

static constexpr uint16_t RP_FS_MAILBOX_MAX_PATH = 96;
static constexpr uint16_t RP_FS_MAILBOX_83_STRING_MAX = 13;
static constexpr uint16_t RP_FS_MAILBOX_CHUNK_SIZE = 256;
static constexpr uint16_t RP_FS_BULK_ARGS_SIZE = 8;

static uint32_t gFSCommandCount = 0;
static uint32_t gFSStatusCount = 0;
static uint32_t gFSOpenCount = 0;
static uint32_t gFSReadCount = 0;
static uint32_t gFSWriteCount = 0;
static uint32_t gFSCloseCount = 0;
static uint32_t gFSLoadCount = 0;
static uint32_t gFSSaveCount = 0;
static uint32_t gFSSeekCount = 0;
static uint32_t gFSTellCount = 0;
static uint32_t gFSDeleteCount = 0;
static uint32_t gFSRenameCount = 0;
static uint32_t gFSOpendirCount = 0;
static uint32_t gFSReaddirCount = 0;
static uint32_t gFSClosedirCount = 0;
static uint32_t gFSErrorCount = 0;

static uint8_t gFSReadBuffer[RP_FS_MAILBOX_CHUNK_SIZE];
static uint8_t gFSWriteBuffer[RP_FS_MAILBOX_CHUNK_SIZE];


struct RPFSBulkArgs {
  uint16_t path_ptr;
  uint16_t mem_ptr;
  uint16_t byte_count;
  uint8_t device;
  uint8_t flags;
};

struct RPFSContextAccess {
  bool active;
  sysstate_t saved_state;
  uint8_t saved_context;
};

/// <summary>
/// rp_fs_mb_begin_context_access halts the 6502, selects the trusted kernel-supplied
/// MMU context, and prepares direct 6502 memory access for bulk filesystem commands.
/// The context is not read from user memory.
/// </summary>
/// <param name="context">Trusted caller context from mailbox ARG2L.</param>
/// <param name="access">Saved state/context to restore later.</param>
/// <returns>true when the context was selected; false when the context is invalid.</returns>
static bool rp_fs_mb_begin_context_access(uint8_t context, RPFSContextAccess* access) {
  if (access == nullptr || context >= MAX_MEMORY_CONTEXTS)
    return false;

  access->saved_state = get6502State();
  set6502State(sRPI);
  access->saved_context = getMMUContext();

  if (access->saved_context != context)
    setMMUContext(context);

  access->active = true;
  return true;
}

/// <summary>
/// rp_fs_mb_end_context_access restores the MMU context and 6502 state saved by
/// rp_fs_mb_begin_context_access.
/// </summary>
/// <param name="access">Saved state/context from begin_context_access.</param>
static void rp_fs_mb_end_context_access(RPFSContextAccess* access) {
  if (access == nullptr || !access->active)
    return;

  if (getMMUContext() != access->saved_context)
    setMMUContext(access->saved_context);

  set6502State(access->saved_state);
  access->active = false;
}

/// <summary>
/// rp_fs_mb_effective_16bit_size converts the NEOX convention where a 16-bit
/// zero size means a full 65536-byte transfer.
/// </summary>
/// <param name="size_word">16-bit NEOX size value.</param>
/// <returns>Effective byte count in the range 1..65536, or 65536 for zero.</returns>
static uint32_t rp_fs_mb_effective_16bit_size(uint16_t size_word) {
  return size_word == 0 ? 65536UL : (uint32_t)size_word;
}

/// <summary>
/// rp_fs_mb_valid_64k_range checks whether a transfer fits inside one 6502
/// 64K context without unintended address wrap. A 65536-byte transfer is valid
/// only when it starts at $0000.
/// </summary>
/// <param name="start">16-bit 6502 start address.</param>
/// <param name="count">Effective byte count.</param>
/// <returns>true when the address range is valid.</returns>
static bool rp_fs_mb_valid_64k_range(uint16_t start, uint32_t count) {
  if (count == 0)
    return true;

  if (count > 65536UL)
    return false;

  if (count == 65536UL)
    return start == 0;

  return ((uint32_t)start + count - 1) <= 0xFFFFUL;
}

/// <summary>
/// rp_fs_mb_read_context_bytes reads bytes from the currently selected 6502
/// context. The caller must have started RPFSContextAccess first.
/// </summary>
/// <param name="src">6502 source address.</param>
/// <param name="len">Number of bytes to read.</param>
/// <param name="dst">RP-local destination buffer.</param>
static void rp_fs_mb_read_context_bytes(uint16_t src, uint32_t len, uint8_t* dst) {
  for (uint32_t i = 0; i < len; i++)
    dst[i] = read6502Memory((uint16_t)(src + i));
}

/// <summary>
/// rp_fs_mb_write_context_bytes writes bytes to the currently selected 6502
/// context. The caller must have started RPFSContextAccess first.
/// </summary>
/// <param name="dst">6502 destination address.</param>
/// <param name="len">Number of bytes to write.</param>
/// <param name="src">RP-local source buffer.</param>
static void rp_fs_mb_write_context_bytes(uint16_t dst, uint32_t len, const uint8_t* src) {
  for (uint32_t i = 0; i < len; i++)
    write6502Memory((uint16_t)(dst + i), src[i]);
}

/// <summary>
/// rp_fs_mb_read_bulk_args reads the fixed 8-byte fs_load_args/fs_save_args
/// block from the trusted caller context.
/// </summary>
/// <param name="args_ptr">6502 pointer to the argument block.</param>
/// <param name="args">Decoded argument block.</param>
/// <returns>true when the block was read and decoded.</returns>
static bool rp_fs_mb_read_bulk_args(uint16_t args_ptr, RPFSBulkArgs* args) {
  if (args_ptr == 0 || args == nullptr)
    return false;

  uint8_t raw[RP_FS_BULK_ARGS_SIZE];
  rp_fs_mb_read_context_bytes(args_ptr, RP_FS_BULK_ARGS_SIZE, raw);

  args->path_ptr = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
  args->mem_ptr = (uint16_t)raw[2] | ((uint16_t)raw[3] << 8);
  args->byte_count = (uint16_t)raw[4] | ((uint16_t)raw[5] << 8);
  args->device = raw[6];
  args->flags = raw[7];
  return true;
}

/// <summary>
/// rp_fs_mb_read_filename_from_current_context reads a NUL-terminated 8.3 name
/// string from the currently selected caller context. The string must terminate
/// within 12 visible characters plus NUL.
/// </summary>
/// <param name="src">6502 pointer to the filename string.</param>
/// <param name="dst">RP-local destination buffer.</param>
/// <param name="dst_len">Size of the destination buffer.</param>
/// <returns>true when the name was read.</returns>
static bool rp_fs_mb_read_filename_from_current_context(uint16_t src, char* dst, size_t dst_len) {
  if (src == 0 || dst == nullptr || dst_len < RP_FS_MAILBOX_83_STRING_MAX)
    return false;

  for (uint16_t i = 0; i < RP_FS_MAILBOX_83_STRING_MAX; i++) {
    uint8_t const c = read6502Memory((uint16_t)(src + i));

    if (c == 0) {
      dst[i] = '\0';
      return i != 0;
    }

    if (c < 32 || c >= 127)
      return false;

    dst[i] = (char)c;
  }

  dst[0] = '\0';
  return false;
}

static void rp_fs_mb_set_done(uint16_t result, uint8_t flags = 0) {
  rp_mailbox_set_done(result, flags);
}

static void rp_fs_mb_set_done32(uint32_t result, uint8_t flags = 0) {
  rp_mailbox_set_done32(result, flags);
}

static void rp_fs_mb_set_error(uint8_t err, uint16_t partial = 0) {
  gFSErrorCount++;
  rp_mailbox_set_error(err, partial);
}

/// <summary>
/// rp_fs_mb_read_filename copies a bounded NUL-terminated filename from 6502
/// RAM into an RP-local C string. It rejects null pointers, empty names, names
/// without a NUL inside ARG1 bytes, strings that exceed the destination buffer,
/// and non-printable/non-ASCII bytes.
/// </summary>
/// <param name="src">6502 source address from ARG0.</param>
/// <param name="scan_len">Maximum number of bytes to scan from ARG1.</param>
/// <param name="dst">RP-local destination buffer.</param>
/// <param name="dst_len">Size of the destination buffer.</param>
/// <returns>true when a valid filename was copied; false on validation failure.</returns>
static bool rp_fs_mb_read_filename(uint16_t src, uint16_t scan_len, char* dst, size_t dst_len) {
  if (src == 0 || scan_len == 0 || dst == nullptr || dst_len < 2)
    return false;

  if (scan_len >= dst_len)
    return false;

  for (uint16_t i = 0; i < scan_len; i++) {
    uint8_t const c = snoop_read6502MemoryLoc((uint16_t)(src + i));

    if (c == 0) {
      dst[i] = '\0';
      return i != 0;
    }

    if (c < 32 || c >= 127)
      return false;

    dst[i] = (char)c;
  }

  dst[0] = '\0';
  return false;
}

/// <summary>
/// rp_fs_mb_write_dir_entry serializes one compact directory entry into 6502
/// memory. The layout is name[13], attr, size_lo, size_hi.
/// </summary>
/// <param name="dst">6502 destination pointer.</param>
/// <param name="name">NUL-terminated 8.3 name.</param>
/// <param name="attr">FatFs attribute byte.</param>
/// <param name="size">32-bit file size.</param>
static void rp_fs_mb_write_dir_entry(uint16_t dst, const char* name, uint8_t attr, uint32_t size) {
  uint8_t raw[RP_FS_DIRENT_SIZE]{};
  size_t const len = strlen(name);
  size_t const copy_len = len > 12 ? 12 : len;

  memcpy(raw, name, copy_len);
  raw[13] = attr;
  raw[14] = (uint8_t)(size & 0xFFUL);
  raw[15] = (uint8_t)((size >> 8) & 0xFFUL);
  raw[16] = (uint8_t)((size >> 16) & 0xFFUL);
  raw[17] = (uint8_t)((size >> 24) & 0xFFUL);

  snoop_write6502Memory(dst, RP_FS_DIRENT_SIZE, raw);
}

/// <summary>
/// rp_fs_mb_handle_status processes FS_STATUS. It returns a compact status word
/// in RES0 where bit 0 means at least one FatFs device is mounted, bits 8..15
/// contain the mounted-device count, and FLAGS contains the mounted-device
/// bitmask.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_status() {
  uint8_t const mask = usb_fatfs_mounted_mask();
  uint8_t const count = usb_fatfs_mounted_count();
  uint16_t const status = (mask != 0 ? RP_FS_STATUS_READY : 0) | ((uint16_t)count << 8);
  rp_fs_mb_set_done(status, mask);

  gFSStatusCount++;

  return mbDONE;
}

/// <summary>
/// rp_fs_mb_handle_open processes FS_OPEN. ARG0 is a pointer to a
/// NUL-terminated filename in 6502 RAM, ARG1 bounds the filename scan, ARG2 low
/// byte contains open flags, and ARG2 high byte selects the device/FatFs
/// drive. RES0 receives the RP-side file handle on success.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_open() {
  uint16_t const src = rp_read16(RP_ARG0L);
  uint16_t const scan_len = rp_read16(RP_ARG1L);
  uint16_t const open_arg = rp_read16(RP_ARG2L);
  uint8_t const flags = (uint8_t)(open_arg & 0xFF);
  uint8_t const device = (uint8_t)(open_arg >> 8);

  if (flags != RP_FS_OPEN_READ &&
      flags != RP_FS_OPEN_WRITE_TRUNC &&
      flags != RP_FS_OPEN_WRITE_EXISTING &&
      flags != RP_FS_OPEN_RW_EXISTING &&
      flags != RP_FS_OPEN_RW_CREATE) {
    rp_fs_mb_set_error(RP_ERR_EPERM);
    return mbDONE;
  }

  if (rp_fs_free_handle_count() == 0) {
    rp_fs_mb_set_error(RP_ERR_ENOMEM);
    return mbDONE;
  }

  char filename[RP_FS_MAILBOX_MAX_PATH];
  if (!rp_fs_mb_read_filename(src, scan_len, filename, sizeof(filename))) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  int handle = -1;
  switch (flags) {
    case RP_FS_OPEN_READ:
      handle = rp_fs_open_readonly_83(device, filename);
      break;

    case RP_FS_OPEN_WRITE_TRUNC:
      handle = rp_fs_open_write_truncate_83(device, filename);
      break;

    case RP_FS_OPEN_WRITE_EXISTING:
      handle = rp_fs_open_write_existing_83(device, filename);
      break;

    case RP_FS_OPEN_RW_EXISTING:
      handle = rp_fs_open_rw_existing_83(device, filename);
      break;

    case RP_FS_OPEN_RW_CREATE:
      handle = rp_fs_open_rw_create_83(device, filename);
      break;
  }

  if (handle < 0) {
    if (!rp_fs_ready(device))
      rp_fs_mb_set_error(RP_ERR_EIO);
    else
      rp_fs_mb_set_error(RP_ERR_ENOENT);
    return mbDONE;
  }

  rp_fs_mb_set_done((uint16_t)handle);
  gFSOpenCount++;

  return mbDONE;
}

/// <summary>
/// rp_fs_mb_handle_read processes FS_READ. ARG0 is the 6502 destination address,
/// ARG1 is the requested byte count, and ARG2 is the RP file handle. The request
/// is capped to RP_FS_MAILBOX_CHUNK_SIZE bytes, RES0 receives the number of bytes
/// copied, and RP_FS_FLAG_EOF is set in FLAGS when the handle is at EOF.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_read() {
  uint16_t const dst = rp_read16(RP_ARG0L);
  uint16_t len = rp_read16(RP_ARG1L);
  uint8_t const handle = (uint8_t)(rp_read16(RP_ARG2L) & 0xFF);

  if (dst == 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (!rp_fs_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (len == 0) {
    uint8_t const eof = (rp_fs_position(handle) >= rp_fs_size(handle)) ? RP_FS_FLAG_EOF : 0;
    rp_fs_mb_set_done(0, eof);
    return mbDONE;
  }

  if (len > RP_FS_MAILBOX_CHUNK_SIZE)
    len = RP_FS_MAILBOX_CHUNK_SIZE;

  int const count = rp_fs_read(handle, gFSReadBuffer, len);
  if (count < 0) {
    rp_fs_mb_set_error(RP_ERR_EIO);
    return mbDONE;
  }

  if (count > 0)
    snoop_write6502Memory(dst, (uint32_t)count, gFSReadBuffer);

  uint8_t const eof = (rp_fs_position(handle) >= rp_fs_size(handle)) ? RP_FS_FLAG_EOF : 0;
  rp_fs_mb_set_done((uint16_t)count, eof);

  gFSReadCount++;

  return mbDONE;
}

/// <summary>
/// rp_fs_mb_handle_write processes FS_WRITE. ARG0 is the 6502 source address,
/// ARG1 is the requested byte count, and ARG2 is the RP file handle. The request
/// is capped to RP_FS_MAILBOX_CHUNK_SIZE bytes, RES0 receives the number of bytes
/// written to the file.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_write() {
  uint16_t const src = rp_read16(RP_ARG0L);
  uint16_t len = rp_read16(RP_ARG1L);
  uint8_t const handle = (uint8_t)(rp_read16(RP_ARG2L) & 0xFF);

  if (src == 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (!rp_fs_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (len == 0) {
    rp_fs_mb_set_done(0);
    return mbDONE;
  }

  if (len > RP_FS_MAILBOX_CHUNK_SIZE)
    len = RP_FS_MAILBOX_CHUNK_SIZE;

  snoop_read6502Memory(src, len, gFSWriteBuffer);

  int const count = rp_fs_write(handle, gFSWriteBuffer, len);
  if (count < 0) {
    rp_fs_mb_set_error(RP_ERR_EIO);
    return mbDONE;
  }

  rp_fs_mb_set_done((uint16_t)count);
  gFSWriteCount++;

  return mbDONE;
}

/// <summary>
/// rp_fs_mailbox_handle_seek processes FS_SEEK. ARG0/ARG1 contain a signed
/// 32-bit offset, ARG2L is the RP file handle, and ARG2H is the POSIX-style
/// whence value: SEEK_SET, SEEK_CUR, or SEEK_END. RES0/RES1 receive the new
/// unsigned 32-bit absolute file position.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_seek() {
  uint32_t const offset_bits = (uint32_t)rp_read16(RP_ARG0L) | ((uint32_t)rp_read16(RP_ARG1L) << 16);
  int32_t const offset = (int32_t)offset_bits;
  uint16_t const handle_arg = rp_read16(RP_ARG2L);
  uint8_t const handle = (uint8_t)(handle_arg & 0xFF);
  uint8_t const whence = (uint8_t)(handle_arg >> 8);

  if (!rp_fs_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  int64_t base = 0;
  switch (whence) {
    case RP_FS_SEEK_SET:
      base = 0;
      break;

    case RP_FS_SEEK_CUR:
      base = (int64_t)rp_fs_position(handle);
      break;

    case RP_FS_SEEK_END:
      base = (int64_t)rp_fs_size(handle);
      break;

    default:
      rp_fs_mb_set_error(RP_ERR_EINVAL);
      return mbDONE;
  }

  int64_t const target = base + (int64_t)offset;
  if (target < 0 || target > 0xFFFFFFFFLL) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (!rp_fs_seek(handle, (uint32_t)target)) {
    rp_fs_mb_set_error(RP_ERR_EIO);
    return mbDONE;
  }

  uint32_t const position = rp_fs_position(handle);
  rp_fs_mb_set_done32(position);
  gFSSeekCount++;

  return mbDONE;
}

/// <summary>
/// rp_fs_mailbox_handle_tell processes FS_TELL. ARG2L is the RP file handle.
/// RES0/RES1 receive the current unsigned 32-bit absolute file position.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_tell() {
  uint8_t const handle = (uint8_t)(rp_read16(RP_ARG2L) & 0xFF);

  if (!rp_fs_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  uint32_t const position = rp_fs_position(handle);
  rp_fs_mb_set_done32(position);
  gFSTellCount++;

  return mbDONE;
}

/// <summary>
/// rp_fs_mailbox_handle_delete processes FS_DELETE. ARG0 points to a
/// NUL-terminated filename in 6502 RAM, ARG1 bounds the filename scan, ARG2L is
/// the device/FatFs drive number, and ARG2H must be zero. RES0 is zero on
/// success.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_delete() {
  uint16_t const src = rp_read16(RP_ARG0L);
  uint16_t const scan_len = rp_read16(RP_ARG1L);
  uint16_t const device_arg = rp_read16(RP_ARG2L);
  uint8_t const device = (uint8_t)(device_arg & 0xFF);

  if ((device_arg & 0xFF00) != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  char filename[RP_FS_MAILBOX_MAX_PATH];
  if (!rp_fs_mb_read_filename(src, scan_len, filename, sizeof(filename))) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (!rp_fs_delete_83(device, filename)) {
    if (!rp_fs_ready(device))
      rp_fs_mb_set_error(RP_ERR_EIO);
    else
      rp_fs_mb_set_error(RP_ERR_ENOENT);
    return mbDONE;
  }

  rp_fs_mb_set_done(0);
  gFSDeleteCount++;
  return mbDONE;
}

/// <summary>
/// rp_fs_mailbox_handle_rename processes FS_RENAME. ARG0 points to the old
/// NUL-terminated filename in 6502 RAM, ARG1 points to the new filename, ARG2L
/// bounds both filename scans, and ARG2H is the device/FatFs drive number. RES0
/// is zero on success.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_rename() {
  uint16_t const old_src = rp_read16(RP_ARG0L);
  uint16_t const new_src = rp_read16(RP_ARG1L);
  uint16_t const rename_arg = rp_read16(RP_ARG2L);
  uint16_t const scan_len = (uint16_t)(rename_arg & 0xFF);
  uint8_t const device = (uint8_t)(rename_arg >> 8);

  if (scan_len == 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  char old_filename[RP_FS_MAILBOX_MAX_PATH];
  char new_filename[RP_FS_MAILBOX_MAX_PATH];
  if (!rp_fs_mb_read_filename(old_src, scan_len, old_filename, sizeof(old_filename)) ||
      !rp_fs_mb_read_filename(new_src, scan_len, new_filename, sizeof(new_filename))) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (!rp_fs_rename_83(device, old_filename, new_filename)) {
    if (!rp_fs_ready(device))
      rp_fs_mb_set_error(RP_ERR_EIO);
    else
      rp_fs_mb_set_error(RP_ERR_ENOENT);
    return mbDONE;
  }

  rp_fs_mb_set_done(0);
  gFSRenameCount++;
  return mbDONE;
}

/// <summary>
/// rp_fs_mailbox_handle_opendir processes FS_OPENDIR. ARG0 points to a
/// NUL-terminated explicit directory path in 6502 RAM, ARG1 bounds the path
/// scan, ARG2L is the device/FatFs drive number, and ARG2H is reserved. RES0L
/// receives the RP directory handle.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_opendir() {
  uint16_t const src = rp_read16(RP_ARG0L);
  uint16_t const scan_len = rp_read16(RP_ARG1L);
  uint16_t const device_arg = rp_read16(RP_ARG2L);
  uint8_t const device = (uint8_t)(device_arg & 0xFF);

  if ((device_arg & 0xFF00) != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  char dirname[RP_FS_MAILBOX_MAX_PATH];
  if (!rp_fs_mb_read_filename(src, scan_len, dirname, sizeof(dirname))) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  int const handle = rp_fs_opendir_83(device, dirname);
  if (handle < 0) {
    if (!rp_fs_ready(device))
      rp_fs_mb_set_error(RP_ERR_EIO);
    else
      rp_fs_mb_set_error(RP_ERR_ENOENT);
    return mbDONE;
  }

  rp_fs_mb_set_done((uint16_t)handle);
  gFSOpendirCount++;
  return mbDONE;
}

/// <summary>
/// rp_fs_mailbox_handle_readdir processes FS_READDIR. ARG0 points to the
/// caller entry buffer, ARG1 is the buffer size, ARG2L is the RP directory
/// handle, and ARG2H is reserved. RES0 is 1 when an entry was returned and 0
/// at end of directory; EOF is also reported through RP_FS_FLAG_EOF.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_readdir() {
  uint16_t const dst = rp_read16(RP_ARG0L);
  uint16_t const dst_len = rp_read16(RP_ARG1L);
  uint16_t const dir_arg = rp_read16(RP_ARG2L);
  uint8_t const handle = (uint8_t)(dir_arg & 0xFF);

  if (dst == 0 || dst_len < RP_FS_DIRENT_SIZE || (dir_arg & 0xFF00) != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (!rp_fs_dir_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  char name[13];
  uint8_t attr = 0;
  uint32_t size = 0;
  int const result = rp_fs_readdir(handle, name, sizeof(name), &attr, &size);

  if (result < 0) {
    rp_fs_mb_set_error(RP_ERR_EIO);
    return mbDONE;
  }

  if (result == 0) {
    rp_fs_mb_set_done(0, RP_FS_FLAG_EOF);
    gFSReaddirCount++;
    return mbDONE;
  }

  rp_fs_mb_write_dir_entry(dst, name, attr, size);
  rp_fs_mb_set_done(1, 0);
  gFSReaddirCount++;
  return mbDONE;
}

/// <summary>
/// rp_fs_mailbox_handle_closedir processes FS_CLOSEDIR. ARG2L is the RP
/// directory handle and ARG2H is reserved. RES0 is zero on success.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_closedir() {
  uint16_t const dir_arg = rp_read16(RP_ARG2L);
  uint8_t const handle = (uint8_t)(dir_arg & 0xFF);

  if ((dir_arg & 0xFF00) != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (!rp_fs_dir_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (!rp_fs_closedir(handle)) {
    rp_fs_mb_set_error(RP_ERR_EIO);
    return mbDONE;
  }

  rp_fs_mb_set_done(0);
  gFSClosedirCount++;
  return mbDONE;
}

/// <summary>
/// rp_fs_mb_handle_close processes FS_CLOSE. ARG2 contains the RP file handle.
/// Only currently-open handles are accepted; stale handles after USB removal are
/// rejected because usb_fatfs_unmount() closes all RP filesystem handles.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_close() {
  uint8_t const handle = (uint8_t)(rp_read16(RP_ARG2L) & 0xFF);

  if (!rp_fs_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (!rp_fs_close(handle)) {
    rp_fs_mb_set_error(RP_ERR_EIO);
    return mbDONE;
  }

  rp_fs_mb_set_done(0);
  gFSCloseCount++;

  return mbDONE;
}


/// <summary>
/// rp_fs_mailbox_handle_load processes FS_LOAD. ARG0 points to an 8-byte
/// fs_load_args block in the caller context, ARG1 must be 8, and ARG2L contains
/// the trusted caller context supplied by the NEOX kernel. The user argument
/// block does not contain a context byte.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_load() {
  uint16_t const args_ptr = rp_read16(RP_ARG0L);
  uint16_t const args_len = rp_read16(RP_ARG1L);
  uint16_t const context_arg = rp_read16(RP_ARG2L);
  uint8_t const context = (uint8_t)(context_arg & 0xFF);

  if (args_len != RP_FS_BULK_ARGS_SIZE || (context_arg & 0xFF00) != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  RPFSContextAccess access{};
  if (!rp_fs_mb_begin_context_access(context, &access)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  RPFSBulkArgs args{};
  char filename[RP_FS_MAILBOX_MAX_PATH];
  bool ok = rp_fs_mb_read_bulk_args(args_ptr, &args)
    && args.flags == 0
    && rp_fs_mb_read_filename_from_current_context(args.path_ptr, filename, sizeof(filename));

  rp_fs_mb_end_context_access(&access);

  if (!ok) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  uint32_t const max_bytes = rp_fs_mb_effective_16bit_size(args.byte_count);
  int const handle = rp_fs_open_readonly_83(args.device, filename);
  if (handle < 0) {
    if (!rp_fs_ready(args.device))
      rp_fs_mb_set_error(RP_ERR_EIO);
    else
      rp_fs_mb_set_error(RP_ERR_ENOENT);
    return mbDONE;
  }

  uint32_t const file_size = rp_fs_size((uint8_t)handle);
  uint32_t const transfer_size = file_size < max_bytes ? file_size : max_bytes;

  if (!rp_fs_mb_valid_64k_range(args.mem_ptr, transfer_size)) {
    rp_fs_close((uint8_t)handle);
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  if (!rp_fs_mb_begin_context_access(context, &access)) {
    rp_fs_close((uint8_t)handle);
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  uint32_t total = 0;
  bool failed = false;
  while (total < transfer_size) {
    uint32_t const remaining = transfer_size - total;
    uint16_t const chunk = (uint16_t)(remaining > RP_FS_MAILBOX_CHUNK_SIZE ? RP_FS_MAILBOX_CHUNK_SIZE : remaining);
    int const count = rp_fs_read((uint8_t)handle, gFSReadBuffer, chunk);

    if (count <= 0) {
      failed = true;
      break;
    }

    rp_fs_mb_write_context_bytes((uint16_t)(args.mem_ptr + total), (uint32_t)count, gFSReadBuffer);
    total += (uint32_t)count;
  }

  rp_fs_mb_end_context_access(&access);
  bool const close_ok = rp_fs_close((uint8_t)handle);

  if (failed || !close_ok || total != transfer_size) {
    rp_fs_mb_set_error(RP_ERR_EIO, (uint16_t)total);
    return mbDONE;
  }

  rp_fs_mb_set_done((uint16_t)total);
  gFSLoadCount++;
  return mbDONE;
}

/// <summary>
/// rp_fs_mailbox_handle_save processes FS_SAVE. ARG0 points to an 8-byte
/// fs_save_args block in the caller context, ARG1 must be 8, and ARG2L contains
/// the trusted caller context supplied by the NEOX kernel. The user argument
/// block does not contain a context byte.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_save() {
  uint16_t const args_ptr = rp_read16(RP_ARG0L);
  uint16_t const args_len = rp_read16(RP_ARG1L);
  uint16_t const context_arg = rp_read16(RP_ARG2L);
  uint8_t const context = (uint8_t)(context_arg & 0xFF);

  if (args_len != RP_FS_BULK_ARGS_SIZE || (context_arg & 0xFF00) != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  RPFSContextAccess access{};
  if (!rp_fs_mb_begin_context_access(context, &access)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  RPFSBulkArgs args{};
  char filename[RP_FS_MAILBOX_MAX_PATH];
  bool ok = rp_fs_mb_read_bulk_args(args_ptr, &args)
    && args.flags == 0
    && rp_fs_mb_read_filename_from_current_context(args.path_ptr, filename, sizeof(filename));

  rp_fs_mb_end_context_access(&access);

  if (!ok) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  uint32_t const transfer_size = rp_fs_mb_effective_16bit_size(args.byte_count);
  if (!rp_fs_mb_valid_64k_range(args.mem_ptr, transfer_size)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  int const handle = rp_fs_open_write_truncate_83(args.device, filename);
  if (handle < 0) {
    if (!rp_fs_ready(args.device))
      rp_fs_mb_set_error(RP_ERR_EIO);
    else
      rp_fs_mb_set_error(RP_ERR_ENOENT);
    return mbDONE;
  }

  if (!rp_fs_mb_begin_context_access(context, &access)) {
    rp_fs_close((uint8_t)handle);
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return mbDONE;
  }

  uint32_t total = 0;
  bool failed = false;
  while (total < transfer_size) {
    uint32_t const remaining = transfer_size - total;
    uint16_t const chunk = (uint16_t)(remaining > RP_FS_MAILBOX_CHUNK_SIZE ? RP_FS_MAILBOX_CHUNK_SIZE : remaining);

    rp_fs_mb_read_context_bytes((uint16_t)(args.mem_ptr + total), chunk, gFSWriteBuffer);

    int const count = rp_fs_write((uint8_t)handle, gFSWriteBuffer, chunk);
    if (count != (int)chunk) {
      failed = true;
      if (count > 0)
        total += (uint32_t)count;
      break;
    }

    total += (uint32_t)count;
  }

  rp_fs_mb_end_context_access(&access);
  bool const close_ok = rp_fs_close((uint8_t)handle);

  if (failed || !close_ok || total != transfer_size) {
    rp_fs_mb_set_error(RP_ERR_EIO, (uint16_t)total);
    return mbDONE;
  }

  rp_fs_mb_set_done((uint16_t)total);
  gFSSaveCount++;
  return mbDONE;
}

/// <summary>
/// rp_fs_mailbox_reset closes all RP filesystem handles owned by the mailbox
/// layer. It is used during mailbox initialization/reset so the 6502 side cannot
/// retain stale RP handles.
/// </summary>
void rp_fs_mailbox_reset() {
  rp_fs_close_all();
}

/// <summary>
/// rp_fs_mailbox_print_diag prints mailbox filesystem command counters to the
/// RP serial debug stream. It is diagnostic only and does not change filesystem
/// or mailbox state.
/// </summary>
void rp_fs_mailbox_print_diag() {
  Serial1.print(F("[fs] cmds="));
  Serial1.print(gFSCommandCount);
  Serial1.print(F(" status="));
  Serial1.print(gFSStatusCount);
  Serial1.print(F(" open="));
  Serial1.print(gFSOpenCount);
  Serial1.print(F(" read="));
  Serial1.print(gFSReadCount);
  Serial1.print(F(" write="));
  Serial1.print(gFSWriteCount);
  Serial1.print(F(" close="));
  Serial1.print(gFSCloseCount);
  Serial1.print(F(" load="));
  Serial1.print(gFSLoadCount);
  Serial1.print(F(" save="));
  Serial1.print(gFSSaveCount);
  Serial1.print(F(" seek="));
  Serial1.print(gFSSeekCount);
  Serial1.print(F(" tell="));
  Serial1.print(gFSTellCount);
  Serial1.print(F(" delete="));
  Serial1.print(gFSDeleteCount);
  Serial1.print(F(" rename="));
  Serial1.print(gFSRenameCount);
  Serial1.print(F(" opendir="));
  Serial1.print(gFSOpendirCount);
  Serial1.print(F(" readdir="));
  Serial1.print(gFSReaddirCount);
  Serial1.print(F(" closedir="));
  Serial1.print(gFSClosedirCount);
  Serial1.print(F(" errs="));
  Serial1.println(gFSErrorCount);
}
