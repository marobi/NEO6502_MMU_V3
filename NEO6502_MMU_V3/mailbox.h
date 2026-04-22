#pragma once

// ------------------------------------------------------------
// Doorbell + status in MMU I/O page
// ------------------------------------------------------------
#define RP_DOORBELL     0xD010          // sync with memory.ini
#define RP_STATUS       0xD011
#define RP_IRQ_SOURCE   0xD012          // source of IRQ
#define RP_CONSOLE_PID  0xD013          // console PID

uint8_t getConsolePID();

void setConsolePID(const uint8_t vPID);

void taskMailbox();

void rp_print_diag();

void initMailbox();
