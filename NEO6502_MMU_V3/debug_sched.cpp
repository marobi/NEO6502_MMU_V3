// 
// 
// 
#include <Arduino.h>

#include "mailbox.h"
#include "debug_sched.h"

#include "neobus.h"

#define STDIN      0

#define  MAX_PROCS 4    // system wide
#define  OPEN_MAX  8    // system wide

#define  MAX_FDS   4    // per process

#define  FD_NONE   0xFF

#define  FD_FLAG_READ  0x01
#define  FD_FLAG_WRITE 0x02

#define  SHARED_STATE 0x8000

static char txt_proc_state[5][4] = {
  "EMP",
  "NEW",
  "RDY",
  "RUN",
  "BLK"
};

/// <summary>
/// must be compatible with kernel definition
/// </summary>
struct __attribute__((packed)) scheduler_info_t {
  uint16_t kernel_version;

  uint16_t brk_vector;
  
  uint8_t  rp_lock;
  
  //-----------------------------------------------
  uint8_t current_pid;

  uint8_t proc_state[MAX_PROCS];
  uint8_t proc_context[MAX_PROCS];
  uint8_t proc_sp[MAX_PROCS];
  uint8_t proc_entryL[MAX_PROCS];
  uint8_t proc_entryH[MAX_PROCS];
  uint8_t proc_flags[MAX_PROCS];

  uint8_t sched_lock;

  uint8_t saved_task_pid;

  uint8_t console_owner_pid;
  uint8_t console_wait_pid;

  uint8_t monitor_return_mode;

  //------------------------------------------------
  // file descriptors per process
  uint8_t proc_fd_obj[MAX_PROCS * MAX_FDS];
  uint8_t proc_fd_flags[MAX_PROCS * MAX_FDS];

  // open files
  uint8_t open_type[OPEN_MAX];
  uint8_t open_refcnt[OPEN_MAX];
  uint8_t open_flags[OPEN_MAX];
  uint8_t open_dev[OPEN_MAX];

  //------------------------------------------------
  uint8_t test_ctr1;
  uint8_t test_ctr2;
  uint8_t test_turn;
};

static uint8_t SharedSpace[sizeof(scheduler_info_t)];
static const scheduler_info_t* SharedInfo = (scheduler_info_t*)SharedSpace;

/// <summary>
/// 
/// </summary>
/// <param name="vPID"></param>
static void dumpTask(const uint8_t pid) {
//  Serial1.printf("%d   %s    %02x %02d  %02d  %02x%02x\n", vPID, txt_proc_state[SharedInfo->proc_state[vPID]], SharedInfo->proc_sp[vPID], SharedInfo->proc_context[vPID], SharedInfo->proc_flags[vPID], SharedInfo->proc_entryH[vPID], SharedInfo->proc_entryL[vPID]);

    Serial1.printf(
      "%d   %s    %02x %02d  %02d  %02x%02x | ",
      pid,
      txt_proc_state[SharedInfo->proc_state[pid]],
      SharedInfo->proc_sp[pid],
      SharedInfo->proc_context[pid],
      SharedInfo->proc_flags[pid],
      SharedInfo->proc_entryH[pid],
      SharedInfo->proc_entryL[pid]
    );

    for (uint8_t fd = 0; fd < MAX_FDS; fd++) {
      const uint8_t index = pid * MAX_FDS + fd;
      const uint8_t obj = SharedInfo->proc_fd_obj[index];
      const uint8_t flags = SharedInfo->proc_fd_flags[index];

      if (obj == FD_NONE) {
        Serial1.print("-       ");
        continue;
      }

      Serial1.printf("%d:", obj);

      if (flags & FD_FLAG_READ) {
        Serial1.print("r");
      }

      if (flags & FD_FLAG_WRITE) {
        Serial1.print("w");
      }

      if (pid == SharedInfo->console_wait_pid && fd == STDIN)
        Serial1.print("*");
      else
        Serial1.print(" ");

      Serial1.print("    ");
    }

    Serial1.println();
}

/// <summary>
/// 
/// </summary>
void dumpScheduler() {
  snoop_read6502Memory(SHARED_STATE, sizeof(scheduler_info_t), SharedSpace);    // read struct

  switch (SharedInfo->kernel_version) {
  case 0x0201:
    Serial1.println("---------------------------------------");
    Serial1.printf("Sched Lock        = %d\n", SharedInfo->sched_lock);
    Serial1.printf("Current PID       = %d\n", SharedInfo->current_pid);
    Serial1.printf("Console Owner PID = %d\n", SharedInfo->console_owner_pid);
    Serial1.printf("Console Wait PID  = %d\n", SharedInfo->console_wait_pid);
    Serial1.printf("Saved Task PID    = %d\n", SharedInfo->saved_task_pid);
    Serial1.printf("Mon Return Mode   = %d\n", SharedInfo->monitor_return_mode);
    Serial1.printf("RP_CONSOLE_RDY    = %d\n", snoop_read6502MemoryLoc(RP_CONSOLE_RDY));
    Serial1.printf("RP_CONSOLE_PID    = %d\n\n", snoop_read6502MemoryLoc(RP_CONSOLE_PID));

#if 0
    Serial1.printf("States: %02s %02s %02s %02s\n", txt_proc_state[SharedInfo->proc_state[0]], txt_proc_state[SharedInfo->proc_state[1]], txt_proc_state[SharedInfo->proc_state[2]], txt_proc_state[SharedInfo->proc_state[3]]);
    Serial1.printf("Stack : %02x %02x %02x %02x\n", SharedInfo->proc_sp[0], SharedInfo->proc_sp[1], SharedInfo->proc_sp[2], SharedInfo->proc_sp[3]);
    Serial1.printf("Contxt: %02d %02d %02d %02d\n", SharedInfo->proc_context[0], SharedInfo->proc_context[1], SharedInfo->proc_context[2], SharedInfo->proc_context[3]);
    Serial1.printf("Flags : %02d %02d %02d %02d\n", SharedInfo->proc_flags[0], SharedInfo->proc_flags[1], SharedInfo->proc_flags[2], SharedInfo->proc_flags[3]);

    Serial1.printf("AddrL:  %02x %02x %02x %02x\n", SharedInfo->proc_entryL[0], SharedInfo->proc_entryL[1], SharedInfo->proc_entryL[2], SharedInfo->proc_entryL[3]);
    Serial1.printf("AddrH:  %02x %02x %02x %02x\n", SharedInfo->proc_entryH[0], SharedInfo->proc_entryH[1], SharedInfo->proc_entryH[2], SharedInfo->proc_entryH[3]);
#endif

    Serial1.println("PID State  SP Ctx Flg Addr | FDs");
    for (uint8_t p = 0; p < MAX_PROCS; p++) {
      dumpTask(p);
    }

    Serial1.printf("\n\nCounters: %02X %02X\n", SharedInfo->test_ctr1, SharedInfo->test_ctr2);

    break;

  default:
    Serial1.printf("Incompatible kernel version [%04x]\n", SharedInfo->kernel_version);
    break;
  }
}
