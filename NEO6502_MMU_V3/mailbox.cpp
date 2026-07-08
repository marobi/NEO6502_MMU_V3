// ============================================================
// rp2350_mailbox_arduino.ino
// NEO6502 MMU - RP2350 Arduino mailbox handler
//
// Mailbox ABI v2:
//   - Request/result block starts at RP_REQ_BASE ($E000)
//   - RP_GROUP and RP_CMD are the command identity at block offsets 0/1
//   - RP_DOORBELL is the interrupt-generating MMU I/O register at $D010
//   - RP_DOORBELL is trigger-only; its value is not a command byte
//   - RP-side command dispatch is single central command-table driven
//   - BIOS already uses $D000-$D004
//
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#include "mailbox.h"
#include "input.h"
#include "neobus.h"
#include "mmu.h"
#include "rp_console_io.h"
#include "rp_fs_mailbox.h"

// ------------------------------------------------------------
// Local limits
// ------------------------------------------------------------
#define ARRAY_COUNT(a)          (sizeof(a) / sizeof((a)[0]))

// ------------------------------------------------------------
// FSM
// ------------------------------------------------------------
static mailbox_state_t mailbox_state = mbINIT;

// ------------------------------------------------------------
// Diagnostics
// ------------------------------------------------------------
static uint32_t gPollCount = 0;
static uint32_t gCommandCount = 0;
static uint32_t gErrorCount = 0;
static uint32_t gUnknownCmdCount = 0;

// ------------------------------------------------------------
// Console Input selection
// ------------------------------------------------------------
static uint8_t gConsolePID = 0;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
// Read a little-endian 16-bit value from the shared 6502 mailbox area.
// Keep this as the single helper used by all RP mailbox command handlers.
uint16_t rp_read16(uint16_t addr) {
  uint8_t lo;
  uint8_t hi;

  lo = snoop_read6502MemoryLoc(addr);
  hi = snoop_read6502MemoryLoc((uint16_t)(addr + 1));

  return (uint16_t)lo | ((uint16_t)hi << 8);
}

// Write a little-endian 16-bit value to the shared 6502 mailbox area.
// This avoids duplicate low/high byte handling in individual command modules.
void rp_write16(uint16_t addr, uint16_t value) {
  snoop_write6502MemoryLoc(addr, (uint8_t)(value & 0xFF));
  snoop_write6502MemoryLoc((uint16_t)(addr + 1), (uint8_t)((value >> 8) & 0xFF));
}

/// <summary>
/// rp_clear_doorbell() clears the RP doorbell register after the RP has
/// consumed or completed the current mailbox request.
/// </summary>
static void rp_clear_doorbell() {
  snoop_write6502MemoryLoc(RP_DOORBELL, RP_DOORBELL_NONE);
}

/// <summary>
/// rp_mailbox_clear_result_fields() clears result, error, flags, and mailbox
/// substate fields before a command handler writes its completion state.
/// </summary>
void rp_mailbox_clear_result_fields() {
  rp_write16(RP_RES0L, 0);
  rp_write16(RP_RES1L, 0);
  snoop_write6502MemoryLoc(RP_ERR, RP_ERR_OK);
  snoop_write6502MemoryLoc(RP_FLAGS, 0);
  snoop_write6502MemoryLoc(RP_STATE, 0);
}

/// <summary>
/// rp_mailbox_set_done() writes a successful mailbox result and marks the
/// request block DONE for the 6502 side.
/// </summary>
/// <param name="result">Primary 16-bit result value written to RES0.</param>
/// <param name="flags">Result flags written to RP_FLAGS.</param>
void rp_mailbox_set_done(uint16_t result, uint8_t flags) {
  rp_write16(RP_RES0L, result);
  rp_write16(RP_RES1L, 0);
  snoop_write6502MemoryLoc(RP_ERR, RP_ERR_OK);
  snoop_write6502MemoryLoc(RP_FLAGS, flags);

  snoop_write6502MemoryLoc(RP_STATUS, RP_DONE);
  rp_clear_doorbell();
}

/// <summary>
/// rp_mailbox_set_done32() writes a successful 32-bit mailbox result split
/// across RES0 low word and RES1 high word, then marks the request block DONE.
/// </summary>
/// <param name="result">32-bit result value written to RES0/RES1.</param>
/// <param name="flags">Result flags written to RP_FLAGS.</param>
void rp_mailbox_set_done32(uint32_t result, uint8_t flags) {
  rp_write16(RP_RES0L, (uint16_t)(result & 0xFFFFUL));
  rp_write16(RP_RES1L, (uint16_t)((result >> 16) & 0xFFFFUL));
  snoop_write6502MemoryLoc(RP_ERR, RP_ERR_OK);
  snoop_write6502MemoryLoc(RP_FLAGS, flags);

  snoop_write6502MemoryLoc(RP_STATUS, RP_DONE);
  rp_clear_doorbell();
}

/// <summary>
/// rp_mailbox_set_error() writes a mailbox error result and marks the request
/// block ERROR for the 6502 side.
/// </summary>
/// <param name="err">RP mailbox error code written to RP_ERR.</param>
/// <param name="partial">Partial result written to RES0 when applicable.</param>
void rp_mailbox_set_error(uint8_t err, uint16_t partial) {
  rp_write16(RP_RES0L, partial);
  rp_write16(RP_RES1L, 0);
  snoop_write6502MemoryLoc(RP_ERR, err);
  snoop_write6502MemoryLoc(RP_FLAGS, 0);

  snoop_write6502MemoryLoc(RP_STATUS, RP_ERROR);
  rp_clear_doorbell();

  gErrorCount++;
}

/// <summary>
/// rp_handle_unknown_command() completes the current request with EINVAL when
/// RP_GROUP/RP_CMD does not match an entry in the central command table.
/// </summary>
/// <param name="group">Mailbox command group from RP_GROUP.</param>
/// <param name="cmd">Mailbox command code from RP_CMD.</param>
static void rp_handle_unknown_command(uint8_t group, uint8_t cmd) {
  (void)group;
  (void)cmd;

  gUnknownCmdCount++;
  rp_mailbox_set_error(RP_ERR_EINVAL, 0);
}

uint8_t getConsolePID() {
  return gConsolePID;
}

void setConsolePID(const uint8_t vPID) {
  snoop_write6502MemoryLoc(RP_CONSOLE_PID, vPID);
  gConsolePID = vPID;
}

// ------------------------------------------------------------
// Central table-driven command dispatch
// ------------------------------------------------------------
typedef mailbox_state_t (*rp_mailbox_command_handler_t)();

struct RPMailboxCommandEntry {
  uint8_t group;
  uint8_t cmd;
  const char* name;
  rp_mailbox_command_handler_t handler;
};

static const RPMailboxCommandEntry gMailboxCommands[] = {
  { RP_GROUP_CONSOLE, RP_CON_CMD_WRITE, "console.write", rp_console_io_handle_write       },
  { RP_GROUP_CONSOLE, RP_CON_CMD_READ,  "console.read",  rp_console_io_handle_read        },
  { RP_GROUP_FS,      RP_FS_CMD_STATUS, "fs.status",     rp_fs_mailbox_handle_status  },
  { RP_GROUP_FS,      RP_FS_CMD_OPEN,   "fs.open",       rp_fs_mailbox_handle_open    },
  { RP_GROUP_FS,      RP_FS_CMD_READ,   "fs.read",       rp_fs_mailbox_handle_read    },
  { RP_GROUP_FS,      RP_FS_CMD_CLOSE,  "fs.close",      rp_fs_mailbox_handle_close   },
  { RP_GROUP_FS,      RP_FS_CMD_WRITE,  "fs.write",      rp_fs_mailbox_handle_write   },
  { RP_GROUP_FS,      RP_FS_CMD_LOAD,   "fs.load",       rp_fs_mailbox_handle_load    },
  { RP_GROUP_FS,      RP_FS_CMD_SAVE,   "fs.save",       rp_fs_mailbox_handle_save    },
  { RP_GROUP_FS,      RP_FS_CMD_SEEK,   "fs.seek",       rp_fs_mailbox_handle_seek    },
  { RP_GROUP_FS,      RP_FS_CMD_TELL,   "fs.tell",       rp_fs_mailbox_handle_tell    },
  { RP_GROUP_FS,      RP_FS_CMD_DELETE, "fs.delete",     rp_fs_mailbox_handle_delete  },
  { RP_GROUP_FS,      RP_FS_CMD_RENAME, "fs.rename",     rp_fs_mailbox_handle_rename  },
  { RP_GROUP_FS,      RP_FS_CMD_OPENDIR, "fs.opendir",    rp_fs_mailbox_handle_opendir },
  { RP_GROUP_FS,      RP_FS_CMD_READDIR, "fs.readdir",    rp_fs_mailbox_handle_readdir },
  { RP_GROUP_FS,      RP_FS_CMD_CLOSEDIR,"fs.closedir",   rp_fs_mailbox_handle_closedir},
  { RP_GROUP_FS,      RP_FS_CMD_MKDIR,  "fs.mkdir",      rp_fs_mailbox_handle_mkdir   },
  { RP_GROUP_FS,      RP_FS_CMD_RMDIR,  "fs.rmdir",      rp_fs_mailbox_handle_rmdir   },
};

/// <summary>
/// rp_find_mailbox_command() searches the single central mailbox command table
/// for the supplied group/command pair.
/// </summary>
/// <param name="group">Mailbox command group from RP_GROUP.</param>
/// <param name="cmd">Mailbox command code from RP_CMD.</param>
/// <returns>Pointer to the command entry, or nullptr when the command is unknown.</returns>
static const RPMailboxCommandEntry* rp_find_mailbox_command(uint8_t group, uint8_t cmd) {
  for (uint8_t i = 0; i < ARRAY_COUNT(gMailboxCommands); i++) {
    if (gMailboxCommands[i].group == group && gMailboxCommands[i].cmd == cmd)
      return &gMailboxCommands[i];
  }

  return nullptr;
}

/// <summary>
/// rp_dispatch_mailbox_command() is the single central dispatch for all mailbox
/// commands. It looks up the command in the central table and invokes the
/// handler. If the command is unknown, it sets an error result.
/// </summary>
/// <param name="group">Mailbox command group from RP_GROUP.</param>
/// <param name="cmd">Mailbox command code from RP_CMD.</param>
/// <returns>The next mailbox FSM state selected by the command handler.</returns>
static mailbox_state_t rp_dispatch_mailbox_command(uint8_t group, uint8_t cmd) {
  const RPMailboxCommandEntry* command_entry = rp_find_mailbox_command(group, cmd);

  if (command_entry == nullptr) {
    rp_handle_unknown_command(group, cmd);
    return mbDONE;
  }

  rp_mailbox_clear_result_fields();
  return command_entry->handler();
}

/// <summary>
/// rp_mailbox_dispatch_current_request() reads the ABI v2 command identity from
/// RP_GROUP/RP_CMD and dispatches it through the same central command table used
/// by the live mailbox doorbell path.
/// </summary>
/// <returns>The next mailbox FSM state selected by the command handler.</returns>
static mailbox_state_t rp_mailbox_dispatch_current_request() {
  uint8_t const group = snoop_read6502MemoryLoc(RP_GROUP);
  uint8_t const cmd = snoop_read6502MemoryLoc(RP_CMD);

  return rp_dispatch_mailbox_command(group, cmd);
}

#if 0
static mailbox_state_t last_mailbox_state = mbINIT;
#endif

/// <summary>
/// taskMailbox() services the RP mailbox FSM. It updates console-ready state,
/// consumes trigger-only ABI v2 doorbell requests, dispatches commands through
/// the central command table, and advances long-running console transfers.
/// </summary>
void taskMailbox() {
  static bool lastCheck = true;                     // empty

#if 0
  if (last_mailbox_state != mailbox_state) {
    Serial1.printf("*D: taskMailbox: %d\n", mailbox_state);
      last_mailbox_state = mailbox_state;
  }
#endif

  bool lCheck = isEmptyCPUQ();

  if (lCheck != lastCheck) {
    snoop_write6502MemoryLoc(RP_CONSOLE_RDY, (lCheck) ? 0 : 1);
    lastCheck = lCheck;
  }

  // FSM
  switch (mailbox_state) {
  case mbINIT:
    mailbox_state = mbIDLE;
    break;

  case mbIDLE:
    static uint16_t lCount = 0;

    gPollCount++;

    if (!triggerMMUIO() && (lCount++ % 2500))
      break;

    ackMMUIO();

    if (snoop_read6502MemoryLoc(RP_DOORBELL) != RP_DOORBELL_NONE ||
        snoop_read6502MemoryLoc(RP_STATUS) == RP_BUSY) {
      gCommandCount++;
      mailbox_state = rp_mailbox_dispatch_current_request();
    }

    break;

  case mbWRITE:
    if (rp_console_io_process_write())
      mailbox_state = mbDONE;

    break;

  case mbREAD:
    if (rp_console_io_process_read())
      mailbox_state = mbDONE;

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
  rp_console_io_print_diag();

  Serial1.print(F(" errs="));
  Serial1.print(gErrorCount);
  Serial1.print(F(" unknown="));
  Serial1.println(gUnknownCmdCount);

  rp_fs_mailbox_print_diag();
}

// ------------------------------------------------------------
// Initialization
// ------------------------------------------------------------
/// <summary>
/// initMailbox() initializes the ABI v2 mailbox request/result block, clears
/// pending doorbells/IRQ status, and resets mailbox-owned console/filesystem
/// state.
/// </summary>
void initMailbox() {
  snoop_write6502MemoryLoc(RP_DOORBELL, RP_DOORBELL_NONE);
  snoop_write6502MemoryLoc(RP_GROUP, RP_GROUP_NONE);
  snoop_write6502MemoryLoc(RP_CMD, RP_CON_CMD_NONE);
  snoop_write6502MemoryLoc(RP_STATUS, RP_IDLE);
  snoop_write6502MemoryLoc(RP_ERR, RP_ERR_OK);
  snoop_write6502MemoryLoc(RP_FLAGS, 0);
  snoop_write6502MemoryLoc(RP_STATE, 0);
  snoop_write6502MemoryLoc(RP_ARG0L, 0);
  snoop_write6502MemoryLoc(RP_ARG0H, 0);
  snoop_write6502MemoryLoc(RP_ARG1L, 0);
  snoop_write6502MemoryLoc(RP_ARG1H, 0);
  snoop_write6502MemoryLoc(RP_ARG2L, 0);
  snoop_write6502MemoryLoc(RP_ARG2H, 0);
  rp_write16(RP_RES0L, 0);
  rp_write16(RP_RES1L, 0);

  // Close mailbox-owned file handles on mailbox reinitialization. This is
  // the RP-side reset hook; the 6502 reset-side callout remains separate.
  rp_console_io_reset();
  rp_fs_mailbox_reset();

  snoop_write6502MemoryLoc(RP_CONSOLE_RDY, 0x00); // no input data
  snoop_write6502MemoryLoc(RP_IRQ_SOURCE, 0x00);  // no IRQ
  snoop_write6502MemoryLoc(RP_IRQ_STATE, 0x00);   // no pending IRQ
}
