#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "mailbox.h"

// ============================================================
// rp_fs_mailbox.h
// NEO MMU - filesystem mailbox command bridge
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
/// Handles FS_WRITE for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_write();

/// <summary>
/// Handles FS_CLOSE for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_close();

/// <summary>
/// Handles FS_LOAD for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_load();

/// <summary>
/// Handles FS_SAVE for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_save();

/// <summary>
/// Handles FS_SEEK for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_seek();

/// <summary>
/// Handles FS_TELL for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_tell();

/// <summary>
/// Handles FS_DELETE for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_delete();

/// <summary>
/// Handles FS_RENAME for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_rename();

/// <summary>
/// Handles FS_OPENDIR for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_opendir();

/// <summary>
/// Handles FS_READDIR for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_readdir();

/// <summary>
/// Handles FS_CLOSEDIR for the central mailbox command table.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_closedir();

/// <summary>
/// Closes all RP-side filesystem handles owned by the mailbox layer.
/// </summary>
void rp_fs_mailbox_reset();

/// <summary>
/// Prints RP-side filesystem mailbox diagnostic counters to Serial1.
/// </summary>
void rp_fs_mailbox_print_diag();
