// 
// 
// 
#include <Arduino.h>

#include "debug_sched.h"

#include "neobus.h"

#define MAX_PROCS 4
#define  SHARED_STATE 0x8000

/// <summary>
/// compatible with kernel definition
/// </summary>
struct scheduler_info_t {
  uint16_t brk_vector;
  uint8_t  rp_lock;
  uint8_t current_pid;
  uint8_t proc_state[MAX_PROCS];
  uint8_t proc_context[MAX_PROCS];
  uint8_t proc_sp[MAX_PROCS];
  uint8_t proc_entryL[MAX_PROCS];
  uint8_t proc_entryH[MAX_PROCS];
  uint8_t sched_lock;

  uint8_t saved_task_pid;

  uint8_t console_owner_pid;

  uint8_t monitor_return_mode;

  uint8_t monitor_pid;

  uint8_t test_ctr1;
  uint8_t test_ctr2;
  uint8_t test_turn;
};

static uint8_t SharedSpace[sizeof(scheduler_info_t)];

/// <summary>
/// 
/// </summary>
void dumpScheduler() {
  snoop_read6502Memory(SHARED_STATE, sizeof(scheduler_info_t), SharedSpace);

  scheduler_info_t* SharedInfo = (scheduler_info_t *)SharedSpace;

  Serial1.println("---------------------------------------");
  Serial1.printf("Locked      = %d\n\n", SharedInfo->sched_lock);
  Serial1.printf("Current PID = %d\n", SharedInfo->current_pid);
  Serial1.printf("Console PID = %d\n\n", SharedInfo->console_owner_pid);

  Serial1.printf("States: %02d %02d %02d %02d\n", SharedInfo->proc_state[0], SharedInfo->proc_state[1], SharedInfo->proc_state[2], SharedInfo->proc_state[3]);
  Serial1.printf("Stack : %02x %02x %02x %02x\n", SharedInfo->proc_sp[0], SharedInfo->proc_sp[1], SharedInfo->proc_sp[2], SharedInfo->proc_sp[3]);
  Serial1.printf("Contxt: %02d %02d %02d %02d\n", SharedInfo->proc_context[0], SharedInfo->proc_context[1], SharedInfo->proc_context[2], SharedInfo->proc_context[3]);

  Serial1.printf("AddrL:  %02x %02x %02x %02x\n", SharedInfo->proc_entryL[0], SharedInfo->proc_entryL[1], SharedInfo->proc_entryL[2], SharedInfo->proc_entryL[3]);
  Serial1.printf("AddrH:  %02x %02x %02x %02x\n", SharedInfo->proc_entryH[0], SharedInfo->proc_entryH[1], SharedInfo->proc_entryH[2], SharedInfo->proc_entryH[3]);

  Serial1.printf("\n\nCounters: %02X %02X\n", SharedInfo->test_ctr1, SharedInfo->test_ctr2);
}
