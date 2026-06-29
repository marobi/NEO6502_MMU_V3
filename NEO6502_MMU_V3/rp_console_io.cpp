// ============================================================
// rp_console_io.cpp
// NEO6502 MMU - RP mailbox console I/O command semantics
//
// This module deliberately contains console command behavior only.
// mailbox.cpp owns the mailbox ABI, status/result helpers, central
// command table, command lookup, and top-level FSM.
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#include "rp_console_io.h"
#include "mailbox.h"
#include "input.h"
#include "neobus.h"

// ------------------------------------------------------------
// Local limits
// ------------------------------------------------------------
#define RP_CONSOLE_CHUNK_SIZE   256

// ------------------------------------------------------------
// Long-running console transfer state
// ------------------------------------------------------------
static uint16_t gConsoleTarget = 0;
static uint16_t gConsoleLen = 0;
static uint16_t gConsoleCurrent = 0;
static uint16_t gConsoleRemaining = 0;
static uint8_t  gConsoleBuffer[RP_CONSOLE_CHUNK_SIZE];

// ------------------------------------------------------------
// Diagnostics
// ------------------------------------------------------------
static uint32_t gConsoleReadCount = 0;
static uint32_t gConsoleWriteCount = 0;

// ------------------------------------------------------------
// Local helpers
// ------------------------------------------------------------
static uint16_t rp_console_input_bytes(uint8_t data[], const uint16_t length) {
  uint16_t len;

  for (len = 0; len < length; len++) {
    uint8_t c = readCPUQ();
    data[len] = c;
    if (c == 0)
      break;
  }

  return len;
}

static void rp_console_output_bytes(const uint8_t* buffer, const uint16_t len) {
  for (uint16_t l = 0; l < len; l++) {
    writeVDUQ(buffer[l]);
  }
}

// ------------------------------------------------------------
// Command setup handlers called by mailbox.cpp central table
// ------------------------------------------------------------
mailbox_state_t rp_console_io_handle_write() {
  gConsoleTarget = rp_read16(RP_ARG0L);
  gConsoleLen = rp_read16(RP_ARG1L);

  if (gConsoleLen == 0) {
    rp_mailbox_set_done(0, 0);
    return mbDONE;
  }

  gConsoleRemaining = gConsoleLen;
  gConsoleCurrent = gConsoleTarget;

  return mbWRITE;
}

mailbox_state_t rp_console_io_handle_read() {
  gConsoleTarget = rp_read16(RP_ARG0L);
  gConsoleLen = rp_read16(RP_ARG1L);

  if (gConsoleLen == 0) {
    rp_mailbox_set_done(0, 0);
    return mbDONE;
  }

  if (gConsoleLen > RP_CONSOLE_CHUNK_SIZE)
    gConsoleLen = RP_CONSOLE_CHUNK_SIZE;

  return mbREAD;
}

// ------------------------------------------------------------
// Long-running console transfer processing called by mailbox FSM
// ------------------------------------------------------------
bool rp_console_io_process_write() {
  uint16_t chunk;

  if (gConsoleRemaining != 0) {
    if (gConsoleRemaining > RP_CONSOLE_CHUNK_SIZE)
      chunk = RP_CONSOLE_CHUNK_SIZE;
    else
      chunk = gConsoleRemaining;

    snoop_read6502Memory(gConsoleCurrent, chunk, gConsoleBuffer);

    rp_console_output_bytes(gConsoleBuffer, chunk);

    gConsoleCurrent += chunk;
    gConsoleRemaining -= chunk;

    gConsoleWriteCount++;
    return false;
  }

  rp_mailbox_set_done(gConsoleLen, 0);
  return true;
}

bool rp_console_io_process_read() {
  gConsoleReadCount++;

  uint16_t const count = rp_console_input_bytes(gConsoleBuffer, gConsoleLen);

  if (count == 0)
    return false;

  snoop_write6502Memory(gConsoleTarget, count, gConsoleBuffer);
  rp_mailbox_set_done(count, 0);
  return true;
}

void rp_console_io_reset() {
  gConsoleTarget = 0;
  gConsoleLen = 0;
  gConsoleCurrent = 0;
  gConsoleRemaining = 0;
}

void rp_console_io_print_diag() {
  Serial1.print(F(" console_reads="));
  Serial1.print(gConsoleReadCount);
  Serial1.print(F(" console_writes="));
  Serial1.print(gConsoleWriteCount);
}
