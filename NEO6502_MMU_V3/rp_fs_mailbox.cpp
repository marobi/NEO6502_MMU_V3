#include <Arduino.h>
#include <stdint.h>
#include <string.h>

#include "mailbox.h"
#include "neobus.h"
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
// Seek/sync/directory/long-filename commands are not implemented in this milestone.
// ============================================================

static constexpr uint16_t RP_FS_MAILBOX_MAX_PATH = 96;
static constexpr uint16_t RP_FS_MAILBOX_CHUNK_SIZE = 256;

static uint32_t gFSCommandCount = 0;
static uint32_t gFSStatusCount = 0;
static uint32_t gFSOpenCount = 0;
static uint32_t gFSReadCount = 0;
static uint32_t gFSWriteCount = 0;
static uint32_t gFSCloseCount = 0;
static uint32_t gFSErrorCount = 0;

static uint8_t gFSReadBuffer[RP_FS_MAILBOX_CHUNK_SIZE];
static uint8_t gFSWriteBuffer[RP_FS_MAILBOX_CHUNK_SIZE];

static void rp_fs_mb_set_done(uint16_t result, uint8_t flags = 0) {
  rp_mailbox_set_done(result, flags);
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

  if (flags != RP_FS_OPEN_READ && flags != RP_FS_OPEN_WRITE_TRUNC) {
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

  int const handle = (flags == RP_FS_OPEN_WRITE_TRUNC)
    ? rp_fs_open_write_truncate_83(device, filename)
    : rp_fs_open_readonly_83(device, filename);

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
  Serial1.print(F(" errs="));
  Serial1.println(gFSErrorCount);
}
