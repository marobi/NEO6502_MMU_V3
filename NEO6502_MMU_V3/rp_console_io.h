#pragma once

#include <stdint.h>

#include "mailbox.h"

// ============================================================
// rp_console_io.h
// NEO6502 MMU - RP mailbox console I/O command semantics
//
// mailbox.cpp owns the ABI v2 transport, central command table,
// command lookup, and mailbox status/result fields. This module owns
// only the console command semantics and long-running console transfer
// state used by the mailbox FSM.
// ============================================================

mailbox_state_t rp_console_io_handle_write();
mailbox_state_t rp_console_io_handle_read();

bool rp_console_io_process_write();
bool rp_console_io_process_read();

void rp_console_io_reset();
void rp_console_io_print_diag();
