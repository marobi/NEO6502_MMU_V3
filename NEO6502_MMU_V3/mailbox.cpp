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
#define RP_REQ_BASE     0x02C0

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
#define RP_CMD_BLK_READ     0x20
#define RP_CMD_BLK_WRITE    0x21

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
// Diagnostics
// ------------------------------------------------------------
static uint32_t gPollCount = 0;
static uint32_t gCommandCount = 0;
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
  gErrorCount++;
}

/// <summary>
/// 
/// </summary>
/// <param name="addr"></param>
/// <param name="len"></param>
/// <returns></returns>
static bool rp_is_valid_range(uint16_t addr, uint16_t len) {
  uint32_t start;
  uint32_t end;

  if (len == 0)
    return true;

  start = (uint32_t)addr;
  end = start + (uint32_t)len - 1UL;

  if (end > 0xFFFFUL)
    return false;

  return true;
}

/// <summary>
/// 
/// </summary>
static char tmpText[4][RP_CONSOLE_CHUNK_SIZE] = {
  "This is a test\n",
  "Hello world\n",
  "Neo6502-MMU test\n",
  "Bye bye\n"
};


/// <summary>
/// 
/// </summary>
/// <param name="data"></param>
/// <param name="vLength"></param>
/// <returns></returns>
static uint16_t rp_input_bytes(char data[], uint16_t vLength)
{
  static uint8_t bp = 0;
  uint16_t len;
  uint16_t i;

  len = strlen(tmpText[bp]);
  if (len > vLength)
    len = vLength;

  for (i = 0; i < len; i++)
    data[i] = tmpText[bp][i];

  bp = (bp + 1) % 4;
  return len;
}

/// <summary>
/// 
/// </summary>
/// <param name="data"></param>
/// <param name="len"></param>
/// <returns></returns>
static bool rp_output_bytes(const uint8_t* data, uint16_t len) {
  uint16_t l = 0;

//  written = Serial1.write(data, len);
  while (l < len)
    writeVDUQ(data[l++]);
  return (true);
}

/// <summary>
/// 
/// </summary>
/// <param name="cmd"></param>
/// <param name="arg0"></param>
/// <param name="arg1"></param>
static void rp_debug_request(uint8_t cmd, uint16_t arg0, uint16_t arg1) {
  Serial1.print(F("[rp] cmd=0x"));
  if (cmd < 0x10)
    Serial1.print('0');
  Serial1.print(cmd, HEX);

  Serial1.print(F(" arg0=0x"));
  if (arg0 < 0x1000) Serial1.print('0');
  if (arg0 < 0x0100) Serial1.print('0');
  if (arg0 < 0x0010) Serial1.print('0');
  Serial1.print(arg0, HEX);

  Serial1.print(F(" arg1=0x"));
  if (arg1 < 0x1000) Serial1.print('0');
  if (arg1 < 0x0100) Serial1.print('0');
  if (arg1 < 0x0010) Serial1.print('0');
  Serial1.println(arg1, HEX);
}

// ------------------------------------------------------------
// Command handlers
// ------------------------------------------------------------
static void rp_handle_console_write(void) {
  uint16_t src;
  uint16_t len;
  uint16_t remaining;
  uint16_t current;
  uint16_t chunk;
  static uint8_t buf[RP_CONSOLE_CHUNK_SIZE];

  src = rp_read16(RP_ARG0L);
  len = rp_read16(RP_ARG1L);

  rp_debug_request(RP_CMD_CON_WRITE, src, len);

  if (!rp_is_valid_range(src, len)) {
    rp_set_error(EINVAL, 0);
    return;
  }

  if (len == 0) {
    rp_set_done(0);
    return;
  }

  remaining = len;
  current = src;

  while (remaining != 0) {
    if (remaining > RP_CONSOLE_CHUNK_SIZE)
      chunk = RP_CONSOLE_CHUNK_SIZE;
    else
      chunk = remaining;

    snoop_read6502Memory(current, chunk, buf);

    if (!rp_output_bytes(buf, chunk)) {
      rp_set_error(EIO, (uint16_t)(len - remaining));
      return;
    }

    current = (uint16_t)(current + chunk);
    remaining = (uint16_t)(remaining - chunk);
  }

  gWriteCount++;
  rp_set_done(len);
}

/// <summary>
/// 
/// </summary>
/// <param name=""></param>
static void rp_handle_console_read(void) {
  uint16_t dst;
  uint16_t len;
  uint16_t count = 0;
  static uint8_t buf[RP_CONSOLE_CHUNK_SIZE];

  dst = rp_read16(RP_ARG0L);
  len = rp_read16(RP_ARG1L);

  rp_debug_request(RP_CMD_CON_READ, dst, len);

  if (!rp_is_valid_range(dst, len)) {
    rp_set_error(EINVAL, 0);
    return;
  }

  if (len == 0) {
    rp_set_done(0);
    return;
  }

  if (len > RP_CONSOLE_CHUNK_SIZE)
    len = RP_CONSOLE_CHUNK_SIZE;

#if 0
  while (!Serial1.available());

  while ((count < len) && Serial1.available()) {
    int c;

    c = Serial1.read();
    if (c < 0)
      break;

    buf[count++] = (uint8_t)c;
  }
#endif
  count = rp_input_bytes((char *)buf, RP_CONSOLE_CHUNK_SIZE);

  if (count != 0) {
    snoop_write6502Memory(dst, count, buf);
//    Serial1.printf("rp_handle_console_read: cnt=%d\n", count);
  }

  rp_set_done(count);
}

/// <summary>
/// 
/// </summary>
/// <param name="cmd"></param>
static void rp_handle_unknown_command(uint8_t cmd) {
  (void)cmd;
  gUnknownCmdCount++;
  rp_set_error(EINVAL, 0);
}

static void rp_handle_command(void) {
  uint8_t cmd;

  gCommandCount++;

  cmd = snoop_read6502MemoryLoc(RP_CMD);

  switch (cmd)
  {
  case RP_CMD_CON_WRITE:
    rp_handle_console_write();
    break;

  case RP_CMD_CON_READ:
    rp_handle_console_read();
    break;

  default:
    rp_handle_unknown_command(cmd);
    break;
  }
}

// ------------------------------------------------------------
// Poll function
// ------------------------------------------------------------
void neo6502_mailbox_poll() {
  uint8_t status;

  gPollCount++;

  status = snoop_read6502MemoryLoc(RP_STATUS);

  if (status != RP_BUSY)
    return;

  rp_handle_command();
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
void neo6502_mailbox_init() {
//  snoop_write6502MemoryLoc(RP_CMD, RP_CMD_NONE);
//  snoop_write6502MemoryLoc(RP_ARG0L, 0);
//  snoop_write6502MemoryLoc(RP_ARG0H, 0);
//  snoop_write6502MemoryLoc(RP_ARG1L, 0);
//  snoop_write6502MemoryLoc(RP_ARG1H, 0);
//  snoop_write6502MemoryLoc(RP_ARG2L, 0);
//  snoop_write6502MemoryLoc(RP_ARG2H, 0);
//  snoop_write6502MemoryLoc(RP_ERR, 0);
//  snoop_write6502MemoryLoc(RP_FLAGS, 0);
//  snoop_write6502MemoryLoc(RP_STATE, 0);
//  rp_write16(RP_RES0L, 0);

//  snoop_write6502MemoryLoc(RP_STATUS, RP_IDLE);
}
