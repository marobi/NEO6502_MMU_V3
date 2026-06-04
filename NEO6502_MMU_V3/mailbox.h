#pragma once

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

uint8_t getConsolePID();

void setConsolePID(const uint8_t vPID);

void taskMailbox();

void rp_print_diag();

void initMailbox();
