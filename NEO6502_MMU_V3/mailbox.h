#pragma once

#include <stdint.h>

// ------------------------------------------------------------
// Doorbell in MMU I/O page
// ------------------------------------------------------------
#define RP_DOORBELL     0xD010                      // sync with memory.ini

#define RP_DOORBELL_NONE     0x00
#define RP_DOORBELL_TRIGGER  0x01

// ------------------------------------------------------------
// Fixed request/result block in 6502 RAM
// ABI v2: command identity is RP_GROUP + RP_CMD at the start of the block.
// RP_DOORBELL is trigger-only and does not carry the command byte anymore.
// ------------------------------------------------------------
#define RP_REQ_BASE     0xE000

#define RP_GROUP        (RP_REQ_BASE + 0x00)
#define RP_CMD          (RP_REQ_BASE + 0x01)
#define RP_STATUS       (RP_REQ_BASE + 0x02)
#define RP_ERR          (RP_REQ_BASE + 0x03)
#define RP_FLAGS        (RP_REQ_BASE + 0x04)
#define RP_STATE        (RP_REQ_BASE + 0x05)
#define RP_ARG0L        (RP_REQ_BASE + 0x06)
#define RP_ARG0H        (RP_REQ_BASE + 0x07)
#define RP_ARG1L        (RP_REQ_BASE + 0x08)
#define RP_ARG1H        (RP_REQ_BASE + 0x09)
#define RP_ARG2L        (RP_REQ_BASE + 0x0A)
#define RP_ARG2H        (RP_REQ_BASE + 0x0B)
#define RP_RES0L        (RP_REQ_BASE + 0x0C)
#define RP_RES0H        (RP_REQ_BASE + 0x0D)
#define RP_RES1L        (RP_REQ_BASE + 0x0E)
#define RP_RES1H        (RP_REQ_BASE + 0x0F)

// These bytes remain outside the request/result block.
#define RP_IRQ_SOURCE   0xE010                      // source of IRQ
#define RP_CONSOLE_PID  0xE011                      // console PID
#define RP_CONSOLE_RDY  0xE012                      // console ready: 0 when no input available
#define RP_IRQ_STATE    0xE013                      // IRQ state: 0=none, 1=pending


// ------------------------------------------------------------
// Mailbox FSM states used by the central dispatcher and command modules
// ------------------------------------------------------------
enum mailbox_state_t {
  mbINIT = 0,
  mbIDLE,
  mbWRITE,
  mbREAD,
  mbDONE
};

// ------------------------------------------------------------
// Mailbox status values
// ------------------------------------------------------------
#define RP_IDLE         0
#define RP_BUSY         1
#define RP_DONE         2
#define RP_ERROR        3

// ------------------------------------------------------------
// Mailbox ABI/version values
// ------------------------------------------------------------
#define RP_MAILBOX_ABI_V2_GROUPED  2
#define RP_MAILBOX_ABI_CURRENT     RP_MAILBOX_ABI_V2_GROUPED

// ------------------------------------------------------------
// Mailbox command groups and per-group command values
// ------------------------------------------------------------
#define RP_GROUP_NONE       0x00
#define RP_GROUP_CONSOLE    0x01
#define RP_GROUP_FS         0x02
#define RP_GROUP_SYSTEM     0x03

#define RP_CON_CMD_NONE     0x00
#define RP_CON_CMD_WRITE    0x01
#define RP_CON_CMD_READ     0x02

#define RP_FS_CMD_NONE      0x00
#define RP_FS_CMD_STATUS    0x01
#define RP_FS_CMD_OPEN      0x02
#define RP_FS_CMD_READ      0x03
#define RP_FS_CMD_CLOSE     0x04

// ------------------------------------------------------------
// RP mailbox error codes aligned with the existing RP mailbox ABI.
// Use prefixed names here to avoid clashing with host/Arduino errno.h macros.
// ------------------------------------------------------------
#define RP_ERR_OK           0
#define RP_ERR_EPERM        1
#define RP_ERR_ENOENT       2
#define RP_ERR_EIO          3
#define RP_ERR_ENOMEM       4
#define RP_ERR_EBUSY        5
#define RP_ERR_EINVAL       6
#define RP_ERR_EPIPE        7

// ------------------------------------------------------------
// Filesystem mailbox flags/results
// ------------------------------------------------------------
#define RP_FS_STATUS_READY  0x0001
#define RP_FS_FLAG_EOF      0x01


// ------------------------------------------------------------
// Shared mailbox result helpers
// ------------------------------------------------------------
void rp_mailbox_clear_result_fields();
void rp_mailbox_set_done(uint16_t result, uint8_t flags = 0);
void rp_mailbox_set_error(uint8_t err, uint16_t partial = 0);

// ------------------------------------------------------------
// Shared mailbox memory helpers
// ------------------------------------------------------------
// These helpers read/write little-endian 16-bit fields in the fixed
// request/result block. Filesystem mailbox handling reuses these instead
// of duplicating local read16/write16 functions.
uint16_t rp_read16(uint16_t addr);
void rp_write16(uint16_t addr, uint16_t value);

uint8_t getConsolePID();

void setConsolePID(const uint8_t vPID);

void taskMailbox();

void rp_print_diag();

void initMailbox();
