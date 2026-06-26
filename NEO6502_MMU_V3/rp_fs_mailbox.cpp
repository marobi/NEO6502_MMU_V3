#include <Arduino.h>
#include <stdint.h>
#include <string.h>

#include "mailbox.h"
#include "neobus.h"
#include "rp_fs.h"

// ============================================================
// rp_fs_mailbox.cpp
// NEO MMU - read-only filesystem mailbox command bridge
//
// Command protocol, using the existing mailbox request/result block:
//
//   FS_STATUS:
//     in : none
//     out: RES0 bit 0 = filesystem ready/mounted
//
//   FS_OPEN:
//     in : ARG0 = pointer to NUL-terminated filename in 6502 RAM
//          ARG1 = maximum filename bytes to scan, including NUL
//          ARG2 = open flags; only 0 is accepted for read-only
//     out: RES0 = RP file handle 0..RP_FS_MAX_HANDLES-1
//
//   FS_READ:
//     in : ARG0 = destination pointer in 6502 RAM
//          ARG1 = requested byte count; capped to 256 bytes per command
//          ARG2 = RP file handle
//     out: RES0 = bytes copied to destination
//          FLAGS bit 0 = EOF after this read
//
//   FS_CLOSE:
//     in : ARG2 = RP file handle
//     out: RES0 = 0
//
// No write/seek/sync commands are implemented in this milestone.
// ============================================================

static constexpr uint16_t RP_FS_MAILBOX_MAX_PATH = 96;
static constexpr uint16_t RP_FS_MAILBOX_CHUNK_SIZE = 256;

static uint32_t gFSCommandCount = 0;
static uint32_t gFSStatusCount = 0;
static uint32_t gFSOpenCount = 0;
static uint32_t gFSReadCount = 0;
static uint32_t gFSCloseCount = 0;
static uint32_t gFSErrorCount = 0;

static uint8_t gFSReadBuffer[RP_FS_MAILBOX_CHUNK_SIZE];

/// <summary>
/// rp_fs_mb_clear_result_fields clears the result fields in the mailbox
/// request/result block. It sets RES0 to 0, ERR to RP_ERR_OK, FLAGS to 0,
/// and STATE to 0. This is called before processing each filesystem command
/// so stale results from a previous command cannot leak into the next result.
/// </summary>
static void rp_fs_mb_clear_result_fields() {
  rp_write16(RP_RES0L, 0);
  snoop_write6502MemoryLoc(RP_ERR, RP_ERR_OK);
  snoop_write6502MemoryLoc(RP_FLAGS, 0);
  snoop_write6502MemoryLoc(RP_STATE, 0);
}

/// <summary>
/// rp_fs_mb_set_done writes a successful filesystem mailbox result. RES0 holds
/// the command-specific result value, ERR is cleared, FLAGS is set to the
/// supplied command flags, STATUS becomes RP_DONE, and the doorbell is cleared
/// so the command is acknowledged exactly once.
/// </summary>
/// <param name="result">Command-specific 16-bit result written to RES0.</param>
/// <param name="flags">Command-specific result flags written to RP_FLAGS.</param>
static void rp_fs_mb_set_done(uint16_t result, uint8_t flags = 0) {
  rp_write16(RP_RES0L, result);
  snoop_write6502MemoryLoc(RP_ERR, RP_ERR_OK);
  snoop_write6502MemoryLoc(RP_FLAGS, flags);
  snoop_write6502MemoryLoc(RP_STATUS, RP_DONE);
  snoop_write6502MemoryLoc(RP_DOORBELL, RP_CMD_NONE);
}

/// <summary>
/// rp_fs_mb_set_error writes a failed filesystem mailbox result. RES0 may hold
/// a partial count, ERR receives the RP mailbox error code, FLAGS is cleared,
/// STATUS becomes RP_ERROR, and the doorbell is cleared.
/// </summary>
/// <param name="err">RP mailbox error code to store in RP_ERR.</param>
/// <param name="partial">Optional partial result, usually zero.</param>
static void rp_fs_mb_set_error(uint8_t err, uint16_t partial = 0) {
  rp_write16(RP_RES0L, partial);
  snoop_write6502MemoryLoc(RP_ERR, err);
  snoop_write6502MemoryLoc(RP_FLAGS, 0);
  snoop_write6502MemoryLoc(RP_STATUS, RP_ERROR);
  snoop_write6502MemoryLoc(RP_DOORBELL, RP_CMD_NONE);

  gFSErrorCount++;
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
/// in RES0 where bit 0 means that FatFs is mounted and the RP filesystem handle
/// layer is usable.
/// </summary>
static void rp_fs_mb_handle_status() {
  // More detail can be added later via RP_FLAGS or additional status bits.
  uint16_t const status = rp_fs_ready() ? RP_FS_STATUS_READY : 0;
  rp_fs_mb_set_done(status);

  gFSStatusCount++;
}

/// <summary>
/// rp_fs_mb_handle_open processes FS_OPEN. ARG0 is a pointer to a
/// NUL-terminated filename in 6502 RAM, ARG1 bounds the filename scan, and ARG2
/// must be zero because this milestone only supports read-only opens. RES0
/// receives the RP-side file handle on success.
/// </summary>
static void rp_fs_mb_handle_open() {
  uint16_t const src = rp_read16(RP_ARG0L);
  uint16_t const scan_len = rp_read16(RP_ARG1L);
  uint16_t const flags = rp_read16(RP_ARG2L);

  if (flags != 0) {
    rp_fs_mb_set_error(RP_ERR_EPERM);
    return;
  }

  if (rp_fs_free_handle_count() == 0) {
    rp_fs_mb_set_error(RP_ERR_ENOMEM);
    return;
  }

  char filename[RP_FS_MAILBOX_MAX_PATH];
  if (!rp_fs_mb_read_filename(src, scan_len, filename, sizeof(filename))) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return;
  }

  int const handle = rp_fs_open_readonly_83(filename);
  if (handle < 0) {
    if (!rp_fs_ready())
      rp_fs_mb_set_error(RP_ERR_EIO);
    else
      rp_fs_mb_set_error(RP_ERR_ENOENT);
    return;
  }

  rp_fs_mb_set_done((uint16_t)handle);
  gFSOpenCount++;
}

/// <summary>
/// rp_fs_mb_handle_read processes FS_READ. ARG0 is the 6502 destination address,
/// ARG1 is the requested byte count, and ARG2 is the RP file handle. The request
/// is capped to RP_FS_MAILBOX_CHUNK_SIZE bytes, RES0 receives the number of bytes
/// copied, and RP_FS_FLAG_EOF is set in FLAGS when the handle is at EOF.
/// </summary>
static void rp_fs_mb_handle_read() {
  uint16_t const dst = rp_read16(RP_ARG0L);
  uint16_t len = rp_read16(RP_ARG1L);
  uint8_t const handle = (uint8_t)(rp_read16(RP_ARG2L) & 0xFF);

  if (dst == 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return;
  }

  if (!rp_fs_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return;
  }

  if (len == 0) {
    uint8_t const eof = (rp_fs_position(handle) >= rp_fs_size(handle)) ? RP_FS_FLAG_EOF : 0;
    rp_fs_mb_set_done(0, eof);
    return;
  }

  if (len > RP_FS_MAILBOX_CHUNK_SIZE)
    len = RP_FS_MAILBOX_CHUNK_SIZE;

  int const count = rp_fs_read(handle, gFSReadBuffer, len);
  if (count < 0) {
    rp_fs_mb_set_error(RP_ERR_EIO);
    return;
  }

  if (count > 0)
    snoop_write6502Memory(dst, (uint32_t)count, gFSReadBuffer);

  uint8_t const eof = (rp_fs_position(handle) >= rp_fs_size(handle)) ? RP_FS_FLAG_EOF : 0;
  rp_fs_mb_set_done((uint16_t)count, eof);

  gFSReadCount++;
}

/// <summary>
/// rp_fs_mb_handle_close processes FS_CLOSE. ARG2 contains the RP file handle.
/// Only currently-open handles are accepted; stale handles after USB removal are
/// rejected because usb_fatfs_unmount() closes all RP filesystem handles.
/// </summary>
static void rp_fs_mb_handle_close() {
  uint8_t const handle = (uint8_t)(rp_read16(RP_ARG2L) & 0xFF);

  if (!rp_fs_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
    return;
  }

  if (!rp_fs_close(handle)) {
    rp_fs_mb_set_error(RP_ERR_EIO);
    return;
  }

  rp_fs_mb_set_done(0);
  gFSCloseCount++;
}

/// <summary>
/// rp_fs_mailbox_handle_status is the public entry point for the explicit
/// RP_CMD_FS_STATUS case in mailbox.cpp.
/// </summary>
void rp_fs_mailbox_handle_status() {
  gFSCommandCount++;
  rp_fs_mb_clear_result_fields();
  rp_fs_mb_handle_status();
}

/// <summary>
/// rp_fs_mailbox_handle_open is the public entry point for the explicit
/// RP_CMD_FS_OPEN case in mailbox.cpp.
/// </summary>
void rp_fs_mailbox_handle_open() {
  gFSCommandCount++;
  rp_fs_mb_clear_result_fields();
  rp_fs_mb_handle_open();
}

/// <summary>
/// rp_fs_mailbox_handle_read is the public entry point for the explicit
/// RP_CMD_FS_READ case in mailbox.cpp.
/// </summary>
void rp_fs_mailbox_handle_read() {
  gFSCommandCount++;
  rp_fs_mb_clear_result_fields();
  rp_fs_mb_handle_read();
}

/// <summary>
/// rp_fs_mailbox_handle_close is the public entry point for the explicit
/// RP_CMD_FS_CLOSE case in mailbox.cpp.
/// </summary>
void rp_fs_mailbox_handle_close() {
  gFSCommandCount++;
  rp_fs_mb_clear_result_fields();
  rp_fs_mb_handle_close();
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
  Serial1.print(F(" close="));
  Serial1.print(gFSCloseCount);
  Serial1.print(F(" errs="));
  Serial1.println(gFSErrorCount);
}
