#include <Arduino.h>
#include <stdint.h>
#include <string.h>

#include "mailbox.h"
#include "memory_config.h"
#include "mmu.h"
#include "neobus.h"
#include "p6502.h"
#include "rp_fs.h"
#include "rp_fs_mailbox.h"
#include "rp_fs_path.h"
#include "scheduler.h"
#include "usb_fatfs.h"

// ============================================================
// rp_fs_mailbox.cpp
// NEO MMU - generic filesystem mailbox bridge
//
// Only RP_FS_CMD_EXEC is accepted by mailbox.cpp. RP_STATE selects the
// filesystem operation; ARG0 points to the caller's existing syscall argument
// block; ARG1 contains trusted PID/context; ARG2 carries a trusted RP handle or
// lifecycle arguments. Filesystem policy, path resolution, CWD state, FatFs
// execution, and caller-context copying are RP-owned.
// ============================================================

static constexpr uint16_t RP_FS_MAILBOX_CHUNK_SIZE = 256;

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
static uint32_t gFSMkdirCount = 0;
static uint32_t gFSRmdirCount = 0;
static uint32_t gFSErrorCount = 0;

static uint8_t gFSReadBuffer[RP_FS_MAILBOX_CHUNK_SIZE];
static uint8_t gFSWriteBuffer[RP_FS_MAILBOX_CHUNK_SIZE];



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


static constexpr uint16_t RP_FS_PRIVATE_LIMIT = 0x6000;

static uint16_t rp_fs_mb_u16(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool rp_fs_mb_private_range(uint16_t address, uint16_t length) {
  if (address == 0 || length == 0 || address >= RP_FS_PRIVATE_LIMIT)
    return false;
  return (uint32_t)address + (uint32_t)length <= RP_FS_PRIVATE_LIMIT;
}

static bool rp_fs_mb_read_context_block(uint8_t context, uint16_t address, uint16_t length, uint8_t* dst) {
  if (dst == nullptr || !rp_fs_mb_private_range(address, length))
    return false;
  RPFSContextAccess access{};
  if (!rp_fs_mb_begin_context_access(context, &access))
    return false;
  rp_fs_mb_read_context_bytes(address, length, dst);
  rp_fs_mb_end_context_access(&access);
  return true;
}

static bool rp_fs_mb_write_context_block(uint8_t context, uint16_t address, uint16_t length, const uint8_t* src) {
  if (src == nullptr || !rp_fs_mb_private_range(address, length))
    return false;
  RPFSContextAccess access{};
  if (!rp_fs_mb_begin_context_access(context, &access))
    return false;
  rp_fs_mb_write_context_bytes(address, length, src);
  rp_fs_mb_end_context_access(&access);
  return true;
}

static bool rp_fs_mb_read_context_path(uint8_t context, uint16_t address, uint16_t max_len, char* dst, size_t dst_len) {
  if (dst == nullptr || dst_len < 2 || max_len == 0 || max_len > dst_len ||
      address == 0 || address >= RP_FS_PRIVATE_LIMIT)
    return false;
  RPFSContextAccess access{};
  if (!rp_fs_mb_begin_context_access(context, &access))
    return false;
  bool ok = false;
  for (uint16_t i = 0; i < max_len; i++) {
    if ((uint32_t)address + i >= RP_FS_PRIVATE_LIMIT)
      break;
    uint8_t const c = read6502Memory((uint16_t)(address + i));
    if (c == 0) {
      dst[i] = '\0';
      ok = i != 0;
      break;
    }
    if (c < 32 || c >= 127)
      break;
    dst[i] = (char)c;
  }
  rp_fs_mb_end_context_access(&access);
  if (!ok)
    dst[0] = '\0';
  return ok;
}

static bool rp_fs_mb_resolve_arg_path(uint8_t pid, uint8_t context, uint16_t path_ptr, uint16_t max_len,
                                      uint8_t fallback_device, RPFSResolvedPath* resolved) {
  char input[RP_FS_NEOX_PATH_MAX];
  if (!rp_fs_mb_read_context_path(context, path_ptr, max_len, input, sizeof(input)))
    return false;
  return rp_fs_resolve_process_path(pid, fallback_device, input, resolved) == RP_FS_PATH_OK;
}

static mailbox_state_t rp_fs_mb_exec_open(uint8_t pid, uint8_t context, uint16_t args_ptr) {
  uint8_t raw[6];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  uint8_t const mode = raw[4];
  if (mode > RP_FS_OPEN_RW_CREATE || raw[5] > 9) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  RPFSResolvedPath path{};
  if (!rp_fs_mb_resolve_arg_path(pid, context, rp_fs_mb_u16(raw), rp_fs_mb_u16(raw + 2), raw[5], &path)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  if (rp_fs_free_handle_count() == 0) {
    rp_fs_mb_set_error(RP_ERR_ENOMEM); return mbDONE;
  }
  int handle = -1;
  switch (mode) {
    case RP_FS_OPEN_READ: handle = rp_fs_open_readonly_83(path.device, path.path); break;
    case RP_FS_OPEN_WRITE_TRUNC: handle = rp_fs_open_write_truncate_83(path.device, path.path); break;
    case RP_FS_OPEN_WRITE_EXISTING: handle = rp_fs_open_write_existing_83(path.device, path.path); break;
    case RP_FS_OPEN_RW_EXISTING: handle = rp_fs_open_rw_existing_83(path.device, path.path); break;
    case RP_FS_OPEN_RW_CREATE: handle = rp_fs_open_rw_create_83(path.device, path.path); break;
  }
  if (handle < 0) {
    rp_fs_mb_set_error(rp_fs_ready(path.device) ? RP_ERR_ENOENT : RP_ERR_EIO); return mbDONE;
  }
  rp_fs_mb_set_done((uint16_t)handle, path.device);
  gFSOpenCount++;
  return mbDONE;
}

static mailbox_state_t rp_fs_mb_exec_single_path(uint8_t operation, uint8_t pid, uint8_t context, uint16_t args_ptr) {
  uint8_t raw[6];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw) || raw[5] != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  RPFSResolvedPath path{};
  if (!rp_fs_mb_resolve_arg_path(pid, context, rp_fs_mb_u16(raw), rp_fs_mb_u16(raw + 2), raw[4], &path)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  bool ok = false;
  if (operation == RP_FS_OP_DELETE) ok = rp_fs_delete_83(path.device, path.path);
  else if (operation == RP_FS_OP_MKDIR) ok = rp_fs_mkdir_83(path.device, path.path);
  else if (operation == RP_FS_OP_RMDIR) ok = rp_fs_rmdir_83(path.device, path.path);
  if (!ok) {
    rp_fs_mb_set_error(rp_fs_ready(path.device) ? RP_ERR_ENOENT : RP_ERR_EIO); return mbDONE;
  }
  if (operation == RP_FS_OP_DELETE) gFSDeleteCount++;
  else if (operation == RP_FS_OP_MKDIR) gFSMkdirCount++;
  else gFSRmdirCount++;
  rp_fs_mb_set_done(0, path.device);
  return mbDONE;
}

static mailbox_state_t rp_fs_mb_exec_rename(uint8_t pid, uint8_t context, uint16_t args_ptr) {
  uint8_t raw[8];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw) || raw[7] != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  RPFSResolvedPath old_path{}, new_path{};
  uint16_t const max_len = rp_fs_mb_u16(raw + 4);
  if (!rp_fs_mb_resolve_arg_path(pid, context, rp_fs_mb_u16(raw), max_len, raw[6], &old_path) ||
      !rp_fs_mb_resolve_arg_path(pid, context, rp_fs_mb_u16(raw + 2), max_len, raw[6], &new_path) ||
      old_path.device != new_path.device) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  if (!rp_fs_rename_83(old_path.device, old_path.path, new_path.path)) {
    rp_fs_mb_set_error(rp_fs_ready(old_path.device) ? RP_ERR_ENOENT : RP_ERR_EIO); return mbDONE;
  }
  gFSRenameCount++;
  rp_fs_mb_set_done(0, old_path.device);
  return mbDONE;
}

static mailbox_state_t rp_fs_mb_exec_opendir(uint8_t pid, uint8_t context, uint16_t args_ptr) {
  uint8_t raw[6];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw) || raw[5] != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  RPFSResolvedPath path{};
  if (!rp_fs_mb_resolve_arg_path(pid, context, rp_fs_mb_u16(raw), rp_fs_mb_u16(raw + 2), raw[4], &path)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  int const handle = rp_fs_opendir_83(path.device, path.path);
  if (handle < 0) {
    rp_fs_mb_set_error(rp_fs_ready(path.device) ? RP_ERR_ENOENT : RP_ERR_EIO); return mbDONE;
  }
  gFSOpendirCount++;
  rp_fs_mb_set_done((uint16_t)handle, path.device);
  return mbDONE;
}

static mailbox_state_t rp_fs_mb_exec_chdir(uint8_t pid, uint8_t context, uint16_t args_ptr) {
  uint8_t raw[6];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw) || raw[5] != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  RPFSResolvedPath path{};
  if (!rp_fs_mb_resolve_arg_path(pid, context, rp_fs_mb_u16(raw), rp_fs_mb_u16(raw + 2), raw[4], &path)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  int const handle = rp_fs_opendir_83(path.device, path.path);
  if (handle < 0) {
    rp_fs_mb_set_error(rp_fs_ready(path.device) ? RP_ERR_ENOENT : RP_ERR_EIO); return mbDONE;
  }
  bool const close_ok = rp_fs_closedir((uint8_t)handle);
  if (!close_ok || !rp_fs_cwd_set(pid, &path)) {
    rp_fs_mb_set_error(RP_ERR_EIO); return mbDONE;
  }
  rp_fs_mb_set_done(0, path.device);
  return mbDONE;
}

static mailbox_state_t rp_fs_mb_exec_getcwd(uint8_t pid, uint8_t context, uint16_t args_ptr) {
  uint8_t raw[8];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw) || raw[6] != 0 || raw[7] != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  uint16_t const dst = rp_fs_mb_u16(raw);
  uint16_t const dst_len = rp_fs_mb_u16(raw + 2);
  char cwd[RP_FS_NEOX_PATH_MAX];
  size_t result_len = 0;
  if (!rp_fs_cwd_format(pid, cwd, sizeof(cwd), &result_len) || result_len + 1 > dst_len ||
      !rp_fs_mb_write_context_block(context, dst, (uint16_t)(result_len + 1), (const uint8_t*)cwd)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  raw[4] = (uint8_t)(result_len & 0xFF);
  raw[5] = (uint8_t)(result_len >> 8);
  if (!rp_fs_mb_write_context_block(context, (uint16_t)(args_ptr + 4), 2, raw + 4)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  rp_fs_mb_set_done((uint16_t)result_len);
  return mbDONE;
}

static mailbox_state_t rp_fs_mb_exec_bulk(uint8_t operation, uint8_t pid, uint8_t context, uint16_t args_ptr) {
  uint8_t raw[8];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw) || raw[7] != 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  RPFSResolvedPath path{};
  if (!rp_fs_mb_resolve_arg_path(pid, context, rp_fs_mb_u16(raw), RP_FS_NEOX_PATH_MAX - 1, raw[6], &path)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  uint16_t const mem_ptr = rp_fs_mb_u16(raw + 2);
  uint32_t const requested = rp_fs_mb_effective_16bit_size(rp_fs_mb_u16(raw + 4));
  uint32_t transfer_size = requested;
  int handle = -1;
  if (operation == RP_FS_OP_LOAD) {
    handle = rp_fs_open_readonly_83(path.device, path.path);
    if (handle >= 0) {
      uint32_t const size = rp_fs_size((uint8_t)handle);
      if (transfer_size > size) transfer_size = size;
    }
  } else {
    handle = rp_fs_open_write_truncate_83(path.device, path.path);
  }
  if (handle < 0) {
    rp_fs_mb_set_error(rp_fs_ready(path.device) ? RP_ERR_ENOENT : RP_ERR_EIO); return mbDONE;
  }
  if (!rp_fs_mb_valid_64k_range(mem_ptr, transfer_size)) {
    rp_fs_close((uint8_t)handle); rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  RPFSContextAccess access{};
  if (!rp_fs_mb_begin_context_access(context, &access)) {
    rp_fs_close((uint8_t)handle); rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  uint32_t total = 0;
  bool failed = false;
  while (total < transfer_size) {
    uint32_t const remaining = transfer_size - total;
    uint16_t const chunk = (uint16_t)(remaining > RP_FS_MAILBOX_CHUNK_SIZE ? RP_FS_MAILBOX_CHUNK_SIZE : remaining);
    if (operation == RP_FS_OP_LOAD) {
      int const count = rp_fs_read((uint8_t)handle, gFSReadBuffer, chunk);
      if (count <= 0) { failed = true; break; }
      rp_fs_mb_write_context_bytes((uint16_t)(mem_ptr + total), (uint32_t)count, gFSReadBuffer);
      total += (uint32_t)count;
    } else {
      rp_fs_mb_read_context_bytes((uint16_t)(mem_ptr + total), chunk, gFSWriteBuffer);
      int const count = rp_fs_write((uint8_t)handle, gFSWriteBuffer, chunk);
      if (count != (int)chunk) { if (count > 0) total += (uint32_t)count; failed = true; break; }
      total += (uint32_t)count;
    }
  }
  rp_fs_mb_end_context_access(&access);
  bool const close_ok = rp_fs_close((uint8_t)handle);
  if (failed || !close_ok || total != transfer_size) {
    rp_fs_mb_set_error(RP_ERR_EIO, (uint16_t)total); return mbDONE;
  }
  if (operation == RP_FS_OP_LOAD) gFSLoadCount++; else gFSSaveCount++;
  rp_fs_mb_set_done((uint16_t)total, path.device);
  return mbDONE;
}



/// <summary>
/// Executes generic file read using the trusted RP handle and the caller's
/// existing rw_args block. The destination lies in the trusted caller context.
/// </summary>
static mailbox_state_t rp_fs_mb_exec_read(uint8_t context, uint16_t args_ptr, uint8_t handle) {
  uint8_t raw[6];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw) || !rp_fs_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  uint16_t const dst = rp_fs_mb_u16(raw + 2);
  uint16_t len = rp_fs_mb_u16(raw + 4);
  if (dst == 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  if (len == 0) {
    uint8_t const eof = (rp_fs_position(handle) >= rp_fs_size(handle)) ? RP_FS_FLAG_EOF : 0;
    rp_fs_mb_set_done(0, eof);
    return mbDONE;
  }
  if (len > RP_FS_MAILBOX_CHUNK_SIZE)
    len = RP_FS_MAILBOX_CHUNK_SIZE;
  if (!rp_fs_mb_private_range(dst, len)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  int const count = rp_fs_read(handle, gFSReadBuffer, len);
  if (count < 0) {
    rp_fs_mb_set_error(RP_ERR_EIO); return mbDONE;
  }
  if (count > 0 && !rp_fs_mb_write_context_block(context, dst, (uint16_t)count, gFSReadBuffer)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  uint8_t const eof = (rp_fs_position(handle) >= rp_fs_size(handle)) ? RP_FS_FLAG_EOF : 0;
  rp_fs_mb_set_done((uint16_t)count, eof);
  gFSReadCount++;
  return mbDONE;
}

/// <summary>
/// Executes generic file write using the trusted RP handle and the caller's
/// existing rw_args block. The source lies in the trusted caller context.
/// </summary>
static mailbox_state_t rp_fs_mb_exec_write(uint8_t context, uint16_t args_ptr, uint8_t handle) {
  uint8_t raw[6];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw) || !rp_fs_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  uint16_t const src = rp_fs_mb_u16(raw + 2);
  uint16_t len = rp_fs_mb_u16(raw + 4);
  if (src == 0) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  if (len == 0) {
    rp_fs_mb_set_done(0);
    return mbDONE;
  }
  if (len > RP_FS_MAILBOX_CHUNK_SIZE)
    len = RP_FS_MAILBOX_CHUNK_SIZE;
  if (!rp_fs_mb_read_context_block(context, src, len, gFSWriteBuffer)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  int const count = rp_fs_write(handle, gFSWriteBuffer, len);
  if (count < 0) {
    rp_fs_mb_set_error(RP_ERR_EIO); return mbDONE;
  }

  rp_fs_mb_set_done((uint16_t)count);
  gFSWriteCount++;
  return mbDONE;
}

/// <summary>
/// Executes generic seek using the trusted RP handle and the caller's existing
/// seek_args block. The resulting 32-bit position is written back to result_lo
/// and result_hi before mailbox completion is published.
/// </summary>
static mailbox_state_t rp_fs_mb_exec_seek(uint8_t context, uint16_t args_ptr, uint8_t handle) {
  uint8_t raw[10];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw) || !rp_fs_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  uint32_t const offset_bits = (uint32_t)rp_fs_mb_u16(raw + 2) |
                               ((uint32_t)rp_fs_mb_u16(raw + 4) << 16);
  int32_t const offset = (int32_t)offset_bits;
  int64_t base = 0;

  switch (raw[1]) {
    case RP_FS_SEEK_SET: base = 0; break;
    case RP_FS_SEEK_CUR: base = (int64_t)rp_fs_position(handle); break;
    case RP_FS_SEEK_END: base = (int64_t)rp_fs_size(handle); break;
    default: rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  int64_t const target = base + (int64_t)offset;
  if (target < 0 || target > 0xFFFFFFFFLL) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }
  if (!rp_fs_seek(handle, (uint32_t)target)) {
    rp_fs_mb_set_error(RP_ERR_EIO); return mbDONE;
  }

  uint32_t const position = rp_fs_position(handle);
  uint8_t result[4] = {
    (uint8_t)(position & 0xFFUL),
    (uint8_t)((position >> 8) & 0xFFUL),
    (uint8_t)((position >> 16) & 0xFFUL),
    (uint8_t)((position >> 24) & 0xFFUL)
  };
  if (!rp_fs_mb_write_context_block(context, (uint16_t)(args_ptr + 6), sizeof(result), result)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  gFSSeekCount++;
  rp_fs_mb_set_done32(position);
  return mbDONE;
}

/// <summary>
/// Executes generic tell using the trusted RP handle and writes the complete
/// 32-bit position into the caller's tell_args block.
/// </summary>
static mailbox_state_t rp_fs_mb_exec_tell(uint8_t context, uint16_t args_ptr, uint8_t handle) {
  uint8_t raw[6];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw) || !rp_fs_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  uint32_t const position = rp_fs_position(handle);
  uint8_t result[4] = {
    (uint8_t)(position & 0xFFUL),
    (uint8_t)((position >> 8) & 0xFFUL),
    (uint8_t)((position >> 16) & 0xFFUL),
    (uint8_t)((position >> 24) & 0xFFUL)
  };
  if (!rp_fs_mb_write_context_block(context, (uint16_t)(args_ptr + 2), sizeof(result), result)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  gFSTellCount++;
  rp_fs_mb_set_done32(position);
  return mbDONE;
}

/// <summary>
/// Executes generic readdir using the trusted RP directory handle. The entry
/// buffer pointer and size are decoded from the caller's readdir_args block.
/// </summary>
static mailbox_state_t rp_fs_mb_exec_readdir(uint8_t context, uint16_t args_ptr, uint8_t handle) {
  uint8_t raw[6];
  if (!rp_fs_mb_read_context_block(context, args_ptr, sizeof(raw), raw) || !rp_fs_dir_is_open(handle)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  uint16_t const dst = rp_fs_mb_u16(raw + 2);
  uint16_t const dst_len = rp_fs_mb_u16(raw + 4);
  if (dst_len < RP_FS_DIRENT_SIZE || !rp_fs_mb_private_range(dst, RP_FS_DIRENT_SIZE)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  char name[13];
  uint8_t attr = 0;
  uint32_t size = 0;
  int const read_result = rp_fs_readdir(handle, name, sizeof(name), &attr, &size);
  if (read_result < 0) {
    rp_fs_mb_set_error(RP_ERR_EIO); return mbDONE;
  }
  if (read_result == 0) {
    gFSReaddirCount++;
    rp_fs_mb_set_done(0, RP_FS_FLAG_EOF);
    return mbDONE;
  }

  uint8_t entry[RP_FS_DIRENT_SIZE]{};
  size_t const name_len = strlen(name);
  size_t const copy_len = name_len > 12 ? 12 : name_len;
  memcpy(entry, name, copy_len);
  entry[13] = attr;
  entry[14] = (uint8_t)(size & 0xFFUL);
  entry[15] = (uint8_t)((size >> 8) & 0xFFUL);
  entry[16] = (uint8_t)((size >> 16) & 0xFFUL);
  entry[17] = (uint8_t)((size >> 24) & 0xFFUL);

  if (!rp_fs_mb_write_context_block(context, dst, sizeof(entry), entry)) {
    rp_fs_mb_set_error(RP_ERR_EINVAL); return mbDONE;
  }

  gFSReaddirCount++;
  rp_fs_mb_set_done(1);
  return mbDONE;
}

/// <summary>
/// Returns compact filesystem mount status through the generic transport.
/// </summary>
static mailbox_state_t rp_fs_mb_exec_status() {
  uint8_t const mask = usb_fatfs_mounted_mask();
  uint8_t const count = usb_fatfs_mounted_count();
  uint16_t const status = (mask != 0 ? RP_FS_STATUS_READY : 0) | ((uint16_t)count << 8);
  rp_fs_mb_set_done(status, mask);
  gFSStatusCount++;
  return mbDONE;
}

/// <summary>
/// Closes one trusted RP file handle.
/// </summary>
static mailbox_state_t rp_fs_mb_exec_close(uint8_t handle) {
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
/// Closes one trusted RP directory handle.
/// </summary>
static mailbox_state_t rp_fs_mb_exec_closedir(uint8_t handle) {
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
/// rp_fs_mailbox_handle_exec handles the compact generic filesystem transport.
/// RP_STATE carries the operation, ARG0 points to the caller argument block,
/// ARG1 contains trusted request-owner PID/context, and ARG2 contains
/// operation-specific trusted values. Handle-only CLOSE/CLOSEDIR requests use
/// ARG2L; READ/WRITE decode the existing rw_args block in caller context.
/// CWD_INIT_ROOT uses ARG2L=target PID and ARG2H=device; CWD_CLONE uses
/// ARG2L=child PID and ARG2H=parent PID.
/// </summary>
/// <returns>Mailbox FSM state selected by the operation handler.</returns>
mailbox_state_t rp_fs_mailbox_handle_exec() {
  gFSCommandCount++;

  uint8_t const operation = snoop_read6502MemoryLoc(RP_STATE);
  uint8_t const pid = snoop_read6502MemoryLoc(RP_ARG1L);
  uint8_t const context = snoop_read6502MemoryLoc(RP_ARG1H);

  mailbox_state_t next_state = mbDONE;

  if (pid >= RP_FS_NEOX_MAX_PROCS || context >= MAX_MEMORY_CONTEXTS) {
    rp_fs_mb_set_error(RP_ERR_EINVAL);
  }
  else {
    switch (operation) {
      case RP_FS_OP_STATUS:
        next_state = rp_fs_mb_exec_status();
        break;

      case RP_FS_OP_OPEN:
        next_state = rp_fs_mb_exec_open(pid, context, rp_read16(RP_ARG0L));
        break;

      case RP_FS_OP_READ:
        next_state = rp_fs_mb_exec_read(context, rp_read16(RP_ARG0L), snoop_read6502MemoryLoc(RP_ARG2L));
        break;

      case RP_FS_OP_WRITE:
        next_state = rp_fs_mb_exec_write(context, rp_read16(RP_ARG0L), snoop_read6502MemoryLoc(RP_ARG2L));
        break;

      case RP_FS_OP_CLOSE:
        next_state = rp_fs_mb_exec_close(snoop_read6502MemoryLoc(RP_ARG2L));
        break;

      case RP_FS_OP_LOAD:
      case RP_FS_OP_SAVE:
        next_state = rp_fs_mb_exec_bulk(operation, pid, context, rp_read16(RP_ARG0L));
        break;

      case RP_FS_OP_DELETE:
      case RP_FS_OP_MKDIR:
      case RP_FS_OP_RMDIR:
        next_state = rp_fs_mb_exec_single_path(operation, pid, context, rp_read16(RP_ARG0L));
        break;

      case RP_FS_OP_RENAME:
        next_state = rp_fs_mb_exec_rename(pid, context, rp_read16(RP_ARG0L));
        break;

      case RP_FS_OP_OPENDIR:
        next_state = rp_fs_mb_exec_opendir(pid, context, rp_read16(RP_ARG0L));
        break;

      case RP_FS_OP_SEEK:
        next_state = rp_fs_mb_exec_seek(context, rp_read16(RP_ARG0L), snoop_read6502MemoryLoc(RP_ARG2L));
        break;

      case RP_FS_OP_TELL:
        next_state = rp_fs_mb_exec_tell(context, rp_read16(RP_ARG0L), snoop_read6502MemoryLoc(RP_ARG2L));
        break;

      case RP_FS_OP_READDIR:
        next_state = rp_fs_mb_exec_readdir(context, rp_read16(RP_ARG0L), snoop_read6502MemoryLoc(RP_ARG2L));
        break;

      case RP_FS_OP_CLOSEDIR:
        next_state = rp_fs_mb_exec_closedir(snoop_read6502MemoryLoc(RP_ARG2L));
        break;

      case RP_FS_OP_CHDIR:
        next_state = rp_fs_mb_exec_chdir(pid, context, rp_read16(RP_ARG0L));
        break;

      case RP_FS_OP_GETCWD:
        next_state = rp_fs_mb_exec_getcwd(pid, context, rp_read16(RP_ARG0L));
        break;

      case RP_FS_OP_CWD_INIT_ROOT: {
        uint8_t const target_pid = snoop_read6502MemoryLoc(RP_ARG2L);
        uint8_t const device = snoop_read6502MemoryLoc(RP_ARG2H);
        if (rp_fs_cwd_init_root(target_pid, device))
          rp_fs_mb_set_done(0);
        else
          rp_fs_mb_set_error(RP_ERR_EINVAL);
        break;
      }

      case RP_FS_OP_CWD_CLONE: {
        uint8_t const child_pid = snoop_read6502MemoryLoc(RP_ARG2L);
        uint8_t const parent_pid = snoop_read6502MemoryLoc(RP_ARG2H);
        if (rp_fs_cwd_clone(child_pid, parent_pid))
          rp_fs_mb_set_done(0);
        else
          rp_fs_mb_set_error(RP_ERR_EINVAL);
        break;
      }

      default:
        rp_fs_mb_set_error(RP_ERR_EINVAL);
        break;
    }
  }

  // PID 0 uses the same request but polls because it has no resumable task
  // continuation. Normal processes are woken through the retryable FS IRQ.
  if (pid != 0)
    requestFSCompletionIRQ();

  return next_state;
}



/// <summary>
/// rp_fs_mailbox_reset closes all RP filesystem handles and resets all RP-owned
/// process CWD slots to device 0 root. It is used during mailbox initialization
/// so NEOX cannot retain stale RP filesystem state.
/// </summary>
void rp_fs_mailbox_reset() {
  rp_fs_close_all();
  rp_fs_cwd_reset_all();
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
  Serial1.print(F(" mkdir="));
  Serial1.print(gFSMkdirCount);
  Serial1.print(F(" rmdir="));
  Serial1.print(gFSRmdirCount);
  Serial1.print(F(" errs="));
  Serial1.println(gFSErrorCount);
}
