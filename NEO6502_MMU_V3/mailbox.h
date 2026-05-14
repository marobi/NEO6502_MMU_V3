#pragma once

// ------------------------------------------------------------
// Doorbell in MMU I/O page
// ------------------------------------------------------------
#define RP_DOORBELL     0xD010                      // sync with memory.ini

// ------------------------------------------------------------
// Fixed request/result block in 6502 RAM
// ------------------------------------------------------------
#define RP_REQ_BASE     0x80C0

#define RP_ARG0L        (RP_REQ_BASE + 0)
#define RP_ARG0H        (RP_REQ_BASE + 1)
#define RP_ARG1L        (RP_REQ_BASE + 2)
#define RP_ARG1H        (RP_REQ_BASE + 3)
#define RP_ARG2L        (RP_REQ_BASE + 4)
#define RP_ARG2H        (RP_REQ_BASE + 5)
#define RP_RES0L        (RP_REQ_BASE + 6)
#define RP_RES0H        (RP_REQ_BASE + 7)
#define RP_ERR          (RP_REQ_BASE + 8)
#define RP_FLAGS        (RP_REQ_BASE + 9)
#define RP_STATE        (RP_REQ_BASE + 10)

#define RP_STATUS       (RP_REQ_BASE + 11)
#define RP_IRQ_SOURCE   (RP_REQ_BASE + 12)          // source of IRQ
#define RP_CONSOLE_PID  (RP_REQ_BASE + 13)          // console PID
#define RP_CONSOLE_RDY  (RP_REQ_BASE + 14)          // console ready: 0 when no input available

uint8_t getConsolePID();

void setConsolePID(const uint8_t vPID);

void taskMailbox();

void rp_print_diag();

void initMailbox();
