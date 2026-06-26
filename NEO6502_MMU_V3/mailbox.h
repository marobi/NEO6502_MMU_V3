#pragma once

#include <stdint.h>

// ------------------------------------------------------------
// Doorbell in MMU I/O page
// ------------------------------------------------------------
#define RP_DOORBELL     0xD010                      // sync with memory.ini

// ------------------------------------------------------------
// Fixed request/result block in 6502 RAM
// ------------------------------------------------------------
#define RP_REQ_BASE     0xE000

#define RP_ARG0L        (RP_REQ_BASE + 0x00)
#define RP_ARG0H        (RP_REQ_BASE + 0x01)
#define RP_ARG1L        (RP_REQ_BASE + 0x02)
#define RP_ARG1H        (RP_REQ_BASE + 0x03)
#define RP_ARG2L        (RP_REQ_BASE + 0x04)
#define RP_ARG2H        (RP_REQ_BASE + 0x05)
#define RP_RES0L        (RP_REQ_BASE + 0x06)
#define RP_RES0H        (RP_REQ_BASE + 0x07)
#define RP_ERR          (RP_REQ_BASE + 0x08)
#define RP_FLAGS        (RP_REQ_BASE + 0x09)
#define RP_STATE        (RP_REQ_BASE + 0x0A)

#define RP_STATUS       (RP_REQ_BASE + 0x0B)
#define RP_IRQ_SOURCE   (RP_REQ_BASE + 0x0C)          // source of IRQ
#define RP_CONSOLE_PID  (RP_REQ_BASE + 0x0D)          // console PID
#define RP_CONSOLE_RDY  (RP_REQ_BASE + 0x0E)          // console ready: 0 when no input available
#define RP_IRQ_STATE    (RP_REQ_BASE + 0x0F)          // IRQ state: 0=none, 1=pending

// ------------------------------------------------------------
// Mailbox status values
// ------------------------------------------------------------
#define RP_IDLE         0
#define RP_BUSY         1
#define RP_DONE         2
#define RP_ERROR        3

// ------------------------------------------------------------
// Mailbox command values
// ------------------------------------------------------------
#define RP_CMD_NONE         0x00
#define RP_CMD_CON_WRITE    0x10
#define RP_CMD_CON_READ     0x11

// Read-only filesystem mailbox commands. RP-side only for now; the 6502
// bindings are intentionally kept separate for the next milestone.
#define RP_CMD_FS_STATUS    0x20
#define RP_CMD_FS_OPEN      0x21
#define RP_CMD_FS_READ      0x22
#define RP_CMD_FS_CLOSE     0x23

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
