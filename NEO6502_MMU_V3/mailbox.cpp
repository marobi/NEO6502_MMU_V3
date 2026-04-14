// ============================================================
// rp2350_mailbox_arduino.ino
// NEO6502 MMU - RP2350 Arduino mailbox handler
//
// Mailbox model:
//   - Request/result block in normal 6502 RAM at $02C0
//   - Doorbell/status registers in MMU I/O page
//   - BIOS already uses $D000-$D004
//   - OS uses $D010 = RP_DOORBELL, $D011 = RP_STATUS
//
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#include "mailbox.h"
#include "input.h"
#include "neobus.h"

// ------------------------------------------------------------
// Fixed request/result block in 6502 RAM
// ------------------------------------------------------------
#define RP_REQ_BASE     0x80C0

#define RP_CMD          (RP_REQ_BASE + 0)
#define RP_ARG0L        (RP_REQ_BASE + 1)
#define RP_ARG0H        (RP_REQ_BASE + 2)
#define RP_ARG1L        (RP_REQ_BASE + 3)
#define RP_ARG1H        (RP_REQ_BASE + 4)
#define RP_ARG2L        (RP_REQ_BASE + 5)
#define RP_ARG2H        (RP_REQ_BASE + 6)
#define RP_RES0L        (RP_REQ_BASE + 7)
#define RP_RES0H        (RP_REQ_BASE + 8)
#define RP_ERR          (RP_REQ_BASE + 9)
#define RP_FLAGS        (RP_REQ_BASE + 10)
#define RP_STATE        (RP_REQ_BASE + 11)

// ------------------------------------------------------------
// Doorbell + status in MMU I/O page
// ------------------------------------------------------------
#define RP_DOORBELL     0xD010
#define RP_STATUS       0xD011

// ------------------------------------------------------------
// Status values
// ------------------------------------------------------------
#define RP_IDLE         0
#define RP_BUSY         1
#define RP_DONE         2
#define RP_ERROR        3

// ------------------------------------------------------------
// Command values
// ------------------------------------------------------------
#define RP_CMD_NONE         0x00
#define RP_CMD_CON_WRITE    0x10
#define RP_CMD_CON_READ     0x11

// ------------------------------------------------------------
// Error codes aligned with 6502 side
// ------------------------------------------------------------
#define E_OK           0
#define EPERM          1
#define ENOENT         2
#define EIO            3
#define ENOMEM         4
#define EBUSY          5
#define EINVAL         6
#define EPIPE          7

// ------------------------------------------------------------
// Local limits
// ------------------------------------------------------------
#define RP_CONSOLE_CHUNK_SIZE   256

// ------------------------------------------------------------
// FSM
// ------------------------------------------------------------
enum mbStates_t {
  mbINIT = 0,
  mbIDLE,
  mbWRITE,
  mbREAD,
  mbDONE
};

static mbStates_t mailbox_state = mbINIT;

// ------------------------------------------------------------
// Diagnostics
// ------------------------------------------------------------
static uint32_t gPollCount = 0;
static uint32_t gCommandCount = 0;
static uint32_t gReadCount = 0;
static uint32_t gWriteCount = 0;
static uint32_t gErrorCount = 0;
static uint32_t gUnknownCmdCount = 0;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
static uint16_t rp_read16(uint16_t addr) {
  uint8_t lo;
  uint8_t hi;

  lo = snoop_read6502MemoryLoc(addr);
  hi = snoop_read6502MemoryLoc((uint16_t)(addr + 1));

  return (uint16_t)lo | ((uint16_t)hi << 8);
}

/// <summary>
/// 
/// </summary>
/// <param name="addr"></param>
/// <param name="value"></param>
static void rp_write16(uint16_t addr, uint16_t value) {
  snoop_write6502MemoryLoc(addr, (uint8_t)(value & 0xFF));
  snoop_write6502MemoryLoc((uint16_t)(addr + 1), (uint8_t)((value >> 8) & 0xFF));
}

/// <summary>
/// 
/// </summary>
/// <param name="result"></param>
static void rp_set_done(uint16_t result) {
  rp_write16(RP_RES0L, result);
  snoop_write6502MemoryLoc(RP_ERR, E_OK);

  snoop_write6502MemoryLoc(RP_STATUS, RP_DONE);
  snoop_write6502MemoryLoc(RP_DOORBELL, RP_CMD_NONE);   // reset doorbell
}

/// <summary>
/// 
/// </summary>
/// <param name="err"></param>
/// <param name="partial"></param>
static void rp_set_error(uint8_t err, uint16_t partial) {
  rp_write16(RP_RES0L, partial);
  snoop_write6502MemoryLoc(RP_ERR, err);

  snoop_write6502MemoryLoc(RP_STATUS, RP_ERROR);
  snoop_write6502MemoryLoc(RP_DOORBELL, RP_CMD_NONE);   // reset doorbell
  gErrorCount++;
}

/// <summary>
/// 
/// </summary>
/// <param name="data"></param>
/// <param name="vLength"></param>
/// <returns></returns>
static uint16_t rp_input_bytes(uint8_t data[], const uint16_t vLength) {
  uint16_t len;

  for (len = 0; len < vLength; len++) {
    uint8_t c = readCPUQ();
    data[len] = c;
    if (c == 0)
      break;
  }

  return len;
}

/// <summary>
/// 
/// </summary>
/// <param name="data"></param>
/// <param name="len"></param>
/// <returns></returns>
static void rp_output_bytes(const uint8_t* buffer, const uint16_t len) {
  for (uint16_t l = 0; l < len; l++) {
    writeVDUQ(buffer[l]);
  }
}

/// <summary>
/// 
/// </summary>
/// <param name="cmd"></param>
/// <param name="arg0"></param>
/// <param name="arg1"></param>
static void rp_debug_request(uint8_t cmd, uint16_t arg0, uint16_t arg1) {
  Serial1.printf("[rp] cmd=0x%02X arg0=0x%04X arg1=0x%02X [%d %d]\n", cmd, arg0, arg1, gCommandCount, gErrorCount);
}


// ------------------------------------------------------------
// Command handlers
// ------------------------------------------------------------
static bool rp_handle_console_write_setup(uint16_t* src, uint16_t* len) {
  *src = rp_read16(RP_ARG0L);
  *len = rp_read16(RP_ARG1L);

//  rp_debug_request(RP_CMD_CON_WRITE, *src, *len);

  if (*len == 0) {          // nothing to write into
    rp_set_done(0);

    return false;
  }

  return true;
}

/// <summary>
/// 
/// </summary>
/// <param name=""></param>
/// <returns></returns>
static bool rp_handle_console_read_setup(uint16_t* dst, uint16_t* len) {
  *dst = rp_read16(RP_ARG0L);
  *len = rp_read16(RP_ARG1L);

//  rp_debug_request(RP_CMD_CON_READ, *dst, *len);

  if (*len == 0) {         // nothing to read into
    rp_set_done(0);

    return false;
  }

  if (*len > RP_CONSOLE_CHUNK_SIZE)
    *len = RP_CONSOLE_CHUNK_SIZE;

  return true;
}

/// <summary>
/// 
/// </summary>
/// <param name="cmd"></param>
static void rp_handle_unknown_command(uint8_t cmd) {
  gUnknownCmdCount++;

  rp_set_error(EINVAL, 0);
}

/// <summary>
/// 
/// </summary>
void taskMailbox() {
  uint8_t bell;
  uint8_t cmd;
  static uint16_t target;
  static uint16_t len;
  static uint16_t count;
  static uint16_t current;
  static uint16_t chunk;
  static uint16_t remaining;
  static uint8_t  buffer[RP_CONSOLE_CHUNK_SIZE];

  // FSM
  switch (mailbox_state) {
  case mbINIT:
    mailbox_state = mbIDLE;
    break;

  case mbIDLE:
    gPollCount++;

    bell = snoop_read6502MemoryLoc(RP_DOORBELL);

    if (bell != RP_CMD_NONE) {               // kling!
      gCommandCount++;

      cmd = snoop_read6502MemoryLoc(RP_CMD);
      switch (cmd) {
      case RP_CMD_CON_WRITE:
        if (rp_handle_console_write_setup(&target, &len)) {
          remaining = len;
          current = target;
          mailbox_state = mbWRITE;
        }
        else
          mailbox_state = mbDONE;
        break;

      case RP_CMD_CON_READ:
        if (rp_handle_console_read_setup(&target, &len))
          mailbox_state = mbREAD;
        else
          mailbox_state = mbDONE;
        break;

      default:
        rp_handle_unknown_command(cmd);
        break;
      }
    }

    break;

  case mbWRITE:
    if (remaining != 0) {
      if (remaining > RP_CONSOLE_CHUNK_SIZE)
        chunk = RP_CONSOLE_CHUNK_SIZE;
      else
        chunk = remaining;

      snoop_read6502Memory(current, chunk, buffer);

      rp_output_bytes(buffer, chunk);

      current += chunk;
      remaining -= chunk;

      gWriteCount++;
    }
    else {
      rp_set_done(len);

      mailbox_state = mbDONE;
    }

    break;

  case mbREAD:
    gReadCount++;

    count = rp_input_bytes(buffer, len);

    if (count != 0) {
      snoop_write6502Memory(target, count, buffer);

      rp_set_done(count);

      mailbox_state = mbDONE;
    }

    break;

  case mbDONE:
    mailbox_state = mbIDLE;
    break;

  default:                    // never reached
    mailbox_state = mbIDLE;
    break;
  }
}

// ------------------------------------------------------------
// Optional periodic diagnostics
// ------------------------------------------------------------
void rp_print_diag() {
  static uint32_t lastMs = 0;
  uint32_t now;

  now = millis();

  if ((now - lastMs) < 1000UL)
    return;

  lastMs = now;

  Serial1.print(F("[diag] polls="));
  Serial1.print(gPollCount);
  Serial1.print(F(" cmds="));
  Serial1.print(gCommandCount);
  Serial1.print(F(" writes="));
  Serial1.print(gWriteCount);
  Serial1.print(F(" errs="));
  Serial1.print(gErrorCount);
  Serial1.print(F(" unknown="));
  Serial1.println(gUnknownCmdCount);
}

// ------------------------------------------------------------
// Initialization
// ------------------------------------------------------------
void initMailbox() {
  snoop_write6502MemoryLoc(RP_DOORBELL, RP_CMD_NONE);
  snoop_write6502MemoryLoc(RP_CMD, RP_CMD_NONE);
  //  snoop_write6502MemoryLoc(RP_ARG0L, 0);
  //  snoop_write6502MemoryLoc(RP_ARG0H, 0);
  //  snoop_write6502MemoryLoc(RP_ARG1L, 0);
  //  snoop_write6502MemoryLoc(RP_ARG1H, 0);
  //  snoop_write6502MemoryLoc(RP_ARG2L, 0);
  //  snoop_write6502MemoryLoc(RP_ARG2H, 0);
  //  snoop_write6502MemoryLoc(RP_ERR, 0);
  //  snoop_write6502MemoryLoc(RP_FLAGS, 0);
  //  snoop_write6502MemoryLoc(RP_STATE, 0);
  rp_write16(RP_RES0L, 0);

  snoop_write6502MemoryLoc(RP_STATUS, RP_IDLE);
}
