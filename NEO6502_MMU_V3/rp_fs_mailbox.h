#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "mailbox.h"

// ============================================================
// rp_fs_mailbox.h
// NEO MMU - read-only filesystem mailbox command bridge
//
// RP-side only. The 6502 syscall/kernel bindings are a separate milestone.
// mailbox.cpp owns the single central command table and dispatch.
// This module owns only the filesystem command semantics.
// ============================================================

/// <summary>
/// Handles FS_STATUS for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_status();

/// <summary>
/// Handles FS_OPEN for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_open();

/// <summary>
/// Handles FS_READ for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_read();

/// <summary>
/// Handles FS_CLOSE for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_close();

/// <summary>
/// Closes all RP-side filesystem handles owned by the mailbox layer.
/// </summary>
void rp_fs_mailbox_reset();

/// <summary>
/// Prints RP-side filesystem mailbox diagnostic counters to Serial1.
/// </summary>
void rp_fs_mailbox_print_diag();
