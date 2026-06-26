#pragma once

#include <Arduino.h>

// ============================================================
// rp_fs_mailbox.h
// NEO MMU - read-only filesystem mailbox command bridge
//
// RP-side only. The 6502 syscall/kernel bindings are a separate milestone.
// The command decode remains in mailbox.cpp as explicit switch cases.
// ============================================================

/// <summary>
/// Handles the RP_CMD_FS_STATUS mailbox command and writes the filesystem
/// readiness result to the shared mailbox request/result block.
/// </summary>
void rp_fs_mailbox_handle_status();

/// <summary>
/// Handles the RP_CMD_FS_OPEN mailbox command. The command opens one RP-side
/// read-only filesystem handle from a bounded filename string in 6502 RAM.
/// </summary>
void rp_fs_mailbox_handle_open();

/// <summary>
/// Handles the RP_CMD_FS_READ mailbox command. The command copies bytes from an
/// open RP-side file handle into 6502 RAM and returns the copied byte count.
/// </summary>
void rp_fs_mailbox_handle_read();

/// <summary>
/// Handles the RP_CMD_FS_CLOSE mailbox command. The command closes one open
/// RP-side filesystem handle.
/// </summary>
void rp_fs_mailbox_handle_close();

/// <summary>
/// Closes all RP-side filesystem handles owned by the mailbox layer.
/// </summary>
void rp_fs_mailbox_reset();

/// <summary>
/// Prints RP-side filesystem mailbox diagnostic counters to Serial1.
/// </summary>
void rp_fs_mailbox_print_diag();
