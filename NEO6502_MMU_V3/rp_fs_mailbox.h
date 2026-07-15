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
/// Handles the single generic blocking filesystem request. RP_STATE contains
/// the operation while ARG0/ARG1/ARG2 carry the compact request.
/// </summary>
mailbox_state_t rp_fs_mailbox_handle_exec();

/// <summary>
/// Closes all RP-side filesystem handles owned by the mailbox layer.
/// </summary>
void rp_fs_mailbox_reset();

/// <summary>
/// Prints RP-side filesystem mailbox diagnostic counters to Serial1.
/// </summary>
void rp_fs_mailbox_print_diag();
