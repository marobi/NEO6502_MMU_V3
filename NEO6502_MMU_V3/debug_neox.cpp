// 
// 
// 
#include <Arduino.h>

#include "mmu.h"
#include "mailbox.h"
#include "debug_neox.h"

#include "neobus.h"

// KERNEL CONFIGURATION
#define  MAX_PROCS 5    // system wide
#define  OPEN_MAX  8    // system wide

#define  MAX_FDS   6    // per process

#define  MAX_TIMER 8    // system wide

#define  MAX_PIPES 8    // system wide

// =============================================
#define  SHARED_STATE 0xC800

#define  PIPE_BUF_SIZE  64  // FIXED: must match kernel definition

// =============================================
#define  FD_NONE   0xFF

#define  FD_FLAG_READ  0x01
#define  FD_FLAG_WRITE 0x02

#define STDIN      0

/// <summary>
/// must be compatible with kernel definition
/// </summary>
struct __attribute__((packed)) scheduler_info_t {
  uint16_t kernel_version;

  uint16_t brk_vector;

  uint8_t  rp_lock;
  //-----------------------------------------------
  uint8_t file_io_gate;
  uint8_t file_io_gate_owner;
  uint8_t file_io_gate_phase;
  uint8_t file_io_gate_wait_head;
  uint8_t file_io_gate_wait_tail;
  uint8_t file_io_gate_next[MAX_PROCS];

  uint8_t proc_gate;
  uint8_t proc_gate_owner;
  uint8_t proc_gate_phase;
  uint8_t proc_gate_wait_head;
  uint8_t proc_gate_wait_tail;
  uint8_t proc_gate_next[MAX_PROCS];

  //-----------------------------------------------
  uint8_t active_pid;
  uint8_t sched_cursor_pid;

  uint8_t proc_state[MAX_PROCS];
  uint8_t proc_context[MAX_PROCS];
  uint8_t proc_sp[MAX_PROCS];
  uint8_t proc_entryL[MAX_PROCS];
  uint8_t proc_entryH[MAX_PROCS];
  uint8_t proc_flags[MAX_PROCS];
  uint8_t proc_parent_pid[MAX_PROCS];
  uint8_t proc_signal_pending[MAX_PROCS];

  uint8_t sched_lock;
  uint8_t sched_lock_owner;
  uint8_t sched_lock_phase;
  uint8_t sched_lock_depth;
  uint8_t sched_lock_underflow;

  uint8_t console_owner_pid;

  uint8_t monitor_active;

  uint8_t active_context;

  //------------------------------------------------
  // file descriptors per process
  uint8_t proc_fd_obj[MAX_PROCS * MAX_FDS];
  uint8_t proc_fd_flags[MAX_PROCS * MAX_FDS];

  // open files
  uint8_t open_type[OPEN_MAX];
  uint8_t open_refcnt[OPEN_MAX];
  uint8_t open_flags[OPEN_MAX];
  uint8_t open_dev[OPEN_MAX];
  uint8_t open_file_handle[OPEN_MAX];

  // wait queues
  uint8_t wait_reason[MAX_PROCS];
  uint8_t wait_object[MAX_PROCS];

  // exit codes
  uint8_t proc_exit_code[MAX_PROCS];

  //------------------------------------------------
  uint8_t system_ticks_lo;
  uint8_t system_ticks_hi;

  uint8_t timer_pid[MAX_TIMER];
  uint8_t timer_until_lo[MAX_TIMER];
  uint8_t timer_until_hi[MAX_TIMER];

  //------------------------------------------------
  uint8_t proc_ticks_lo[MAX_PROCS];
  uint8_t proc_ticks_hi[MAX_PROCS];

  //------------------------------------------------
  uint8_t console_read_len_lo;
  uint8_t console_read_len_hi;

  //------------------------------------------------
  uint8_t init_task_count;
  uint8_t init_task_ptrL;
  uint8_t init_task_ptrH;

  uint8_t pipe_state[MAX_PIPES];
  uint8_t pipe_head[MAX_PIPES];
  uint8_t pipe_tail[MAX_PIPES];
  uint8_t pipe_count[MAX_PIPES];
  uint8_t pipe_readers[MAX_PIPES];
  uint8_t pipe_writers[MAX_PIPES];
  uint8_t pipe_buf[MAX_PIPES * PIPE_BUF_SIZE];

  // Per-open-object pipe endpoint metadata.
  // Indexed by open object number.
  uint8_t open_pipe[OPEN_MAX];
  uint8_t open_pipe_mode[OPEN_MAX];

  //================================================
  //
  // debug stuff, not necessarily in kernel 
  // 
  uint8_t sched_debug_marker;
  uint8_t sched_debug_pid;
  uint8_t sched_debug_old_pid;
  uint8_t sched_debug_old_state;

  uint8_t sched_debug_state_pid;
  uint8_t sched_debug_state_old;
  uint8_t sched_debug_state_new;

  uint8_t dbg_sched_path;
  uint8_t dbg_sched_current_pid;
  uint8_t dbg_sched_selected_pid;

  uint8_t dbg_sched_saved_pid;
  uint8_t dbg_sched_saved_sp;
  uint8_t dbg_sched_saved_mode;

  uint8_t dbg_sched_loaded_pid;
  uint8_t dbg_sched_loaded_sp;

  uint8_t dbg_sched_resume_mode;
  uint8_t dbg_sched_resume_pid;
  uint8_t dbg_sched_resume_context;

  uint8_t dbg_proc_state_pid;
  uint8_t dbg_proc_state_old;
  uint8_t dbg_proc_state_new;

  uint8_t rp_lock_owner;

  uint8_t dbg_gate_wait_reason;
  uint8_t dbg_gate_wait_object;

  uint8_t dbg_timer_pid;
  uint8_t dbg_timer_slot;
  uint8_t dbg_timer_until_lo;
  uint8_t dbg_timer_until_hi;
  uint8_t dbg_timer_now_lo;
  uint8_t dbg_timer_now_hi;

  uint8_t dbg_irq_preempt_count;
  uint8_t dbg_irq_current_pid;
  uint8_t dbg_irq_selected_pid;
  uint8_t dbg_irq_saved_sp;
  uint8_t dbg_irq_loaded_sp;
  uint8_t dbg_irq_skip_reason;

};

static uint8_t SharedSpace[sizeof(scheduler_info_t)];
static const scheduler_info_t* SharedInfo = (scheduler_info_t*)SharedSpace;

/*
E_OK                = 0
EIO                 = 3
EINVAL              = 6
EMFILE              = 8        ; too many open file descriptors
EBADF               = 9
ENODEV              = 10
ENOSYS              = 11
EAGAIN              = 12       ; operation would block / try again
ENOMEM              = 13       ; no free kernel memory/object/table slot
EPIPE               = 14       ; write on pipe with no readers
*/
static char txt_errno[15][7] = {
  "OK",
  "?",
  "?",
  "EIO",
  "?",
  "?",
  "EINVAL",
  "?",
  "EMFILE",
  "EBADF",
  "ENODEV",
  "ENOSYS",
  "EAGAIN",
  "ENOMEM",
  "EPIPE"
};

/// <summary>
/// txt for process states, must be compatible with kernel definition
/// </summary>
static char txt_proc_state[7][4] = {
  "EMP",
  "NEW",
  "RDY",
  "RUN",
  "BLK",
  "STP",
  "ZOM"
};

/// <summary>
/// txt for wait reasons, must be compatible with kernel definition
/// </summary>
static const char* txt_wait_reason[] = {
  "-",     // 0 WAIT_NONE
  "CON",   // 1 WAIT_CONSOLE
  "DEV",   // 2 WAIT_DEVICE
  "PIR",   // 3 WAIT_PIPE_READ
  "TIM",   // 4 WAIT_TIMER
  "PRC",   // 5 WAIT_PROC
  "LCK",   // 6 WAIT_LOCK
  "PIW",   // 7 WAIT_PIPE_WRITE
};

#define WAIT_NONE       0x00
#define WAIT_CONSOLE    0x01
#define WAIT_DEVICE     0x02
#define WAIT_PIPE       0x03
#define WAIT_TIMER      0x04
#define WAIT_PROC       0x05
#define WAIT_LOCK       0x06

#define LOCK_ID_FILE_IO 0x00
#define LOCK_ID_PROC    0x01

/// <summary>
/// txt for open file types, must be compatible with kernel definition
/// </summary>
static char txt_open_type[4][4] = {
  "-",
  "DEV",
  "FIL",
  "PIP"
};

/// <summary>
/// txt for device types, must be compatible with kernel definition
/// </summary>
static char txt_dev_type[2][4] = {
  "-",
  "CON",
};

/// <summary>
/// txt for process resume modes, must be compatible with kernel definition
/// </summary>
static char txt_proc_resume_mode[2][4] = {
  "RTI",  // 0 = PROC_RESUME_RTI / PROC_FRAME_RTI
  "?"     // 1 = invalid in RTI-only scheduler
};

/// <summary>
/// txt for pending signals, must be compatible with kernel definition
/// </summary>
static char txt_signal_pending[4][2] = {
  "-",
  "H",
  "C",
  "K"
};

/// <summary>
/// txt for pipe states, must be compatible with kernel definition
/// </summary>
static char txt_pipe_state[2][5] = {
  "FREE",
  "USED"
};

/*
-- = no skip / IRQ entered scheduler
M  = skipped because monitor_active != 0
S  = skipped because sched_lock != 0
R  = skipped because rp_lock != 0
F  = skipped because file_io_gate != 0
P  = skipped because proc_gate != 0
E  = enter switch
*/
static char txt_irq_skip_reason[7][2] = {
  "-",
  "M",
  "S",
  "R",
  "F",
  "P",
  "E"
};

/// <summary>
/// Dump scheduler debug info. Must match kernel/shared_state.asm and include/debug.inc.
/// </summary>
static const char* dbgPathName(uint8_t path) {
  switch (path) {
  case 0x00: return "-";
  case 0x01: return "IRQ";
  case 0x02: return "YLD";
  default:   return "?";
  }
}

/// <summary>
/// debug mode name, must be compatible with kernel definition and usage in scheduler code.
/// </summary>
/// <param name="mode"></param>
/// <returns></returns>
static const char* dbgModeName(uint8_t mode) {
  switch (mode) {
  case 0x00: return "-";
  case 0x01: return "IRQ";   // RTI frame created by IRQ/preemption path
  case 0x02: return "YLD";   // RTI frame created by cooperative yield path
  case 0x03: return "BOOT";  // first-run bootstrap path, if used by kernel debug
  default:   return "?";
  }
}

/// <summary>
/// scheduler marker text, must be compatible with kernel definition.
/// These markers are used to identify where in the scheduler code we are, 
/// for better understanding of scheduler behavior and easier debugging.
/// </summary>
/// <param name="marker"></param>
/// <returns></returns>
static const char* dbgMarkerText(const uint8_t marker) {
  switch (marker) {
  case 0x00: return "-";
  case 0x01: return "IRQ";
  case 0x02: return "SAVE";
  case 0x03: return "PICK";
  case 0x04: return "YLD";
  case 0x61: return "SEL";
  case 0x62: return "LOAD";
  case 0x63: return "RTI";
  case 0x64: return "BOOT";
  case 0xEE: return "STALE";
  case 0xE1: return "GREC";
  case 0xE2: return "GREL";
  default:   return "?";
  }
}

/// <summary>
/// wait object text, must be compatible with kernel definition
/// </summary>
/// <param name="reason"></param>
/// <param name="object"></param>
/// <returns></returns>
static const char* waitObjectText(const uint8_t reason, const uint8_t object) {
  if (reason == WAIT_LOCK) {
    switch (object) {
    case LOCK_ID_FILE_IO:
      return "FIO";
    case LOCK_ID_PROC:
      return "PRC";
    default:
      return "???";
    }
  }

  if (reason == WAIT_NONE) {
    return "--";
  }

  return nullptr;
}

/// <summary>
/// dump pipe info, must be compatible with kernel definition
/// </summary>
static void dumpPipes() {
  Serial1.println("Pipe State Head Tail Count Readers Writers");
  for (uint8_t i = 0; i < MAX_PIPES; i++) {
    Serial1.printf("%d    %4s  %02d   %02d   %02d    %02d      %02d\n",
      i,
      SharedInfo->pipe_state[i] < 2 ? txt_pipe_state[SharedInfo->pipe_state[i]] : "?",
      SharedInfo->pipe_head[i],
      SharedInfo->pipe_tail[i],
      SharedInfo->pipe_count[i],
      SharedInfo->pipe_readers[i],
      SharedInfo->pipe_writers[i]);
  }
}

/// <summary>
/// dump accounting info, must be compatible with kernel definition
/// </summary>
static void dumpAccounting() {
  Serial1.println("PID Ticks");
  for (uint8_t i = 0; i < MAX_PROCS; i++) {
    Serial1.printf("%d   %5d\n", i, (SharedInfo->proc_ticks_hi[i] << 8) | SharedInfo->proc_ticks_lo[i]);
  }
}

/// <summary>
/// dump timer info, must be compatible with kernel definition
/// </summary>
static void dumpTimers() {
  Serial1.println("Timer PID Ticks");
  for (uint8_t i = 0; i < MAX_TIMER; i++) {
    if (SharedInfo->timer_pid[i] != 0xFF) {
      uint16_t until = (SharedInfo->timer_until_hi[i] << 8) | SharedInfo->timer_until_lo[i];
      Serial1.printf("%d     %3d %d\n", i, SharedInfo->timer_pid[i], until);
    }
    else
      Serial1.printf("%d     -\n", i);
  }
}
/// <summary>
/// dump open files, must be compatible with kernel definition
/// </summary>
static void dumpOpenFiles() {
  Serial1.println("FD Type RefCnt Flags Dev");
  for (uint8_t i = 0; i < OPEN_MAX; i++) {
    if (SharedInfo->open_type[i] == FD_NONE) {
      Serial1.print("-       ");
    }
    else {
      Serial1.printf("%d  %3s  %d      %02x    %3s\n",
        i,
        txt_open_type[SharedInfo->open_type[i]],
        SharedInfo->open_refcnt[i],
        SharedInfo->open_flags[i],
        txt_dev_type[SharedInfo->open_dev[i]]);
    }
  }
}

/// <summary>
/// dump task info for given PID, must be compatible with kernel definition
/// </summary>
/// <param name="vPID"></param>
static void dumpTask(const uint8_t pid) {
  const uint8_t waitReason = SharedInfo->wait_reason[pid];
  const uint8_t waitObject = SharedInfo->wait_object[pid];

  const char* waitReasonText =
    waitReason < 7 ? txt_wait_reason[waitReason] : "?";

  const char* waitObjectName = waitObjectText(waitReason, waitObject);

  Serial1.printf(
    " %3d %3d %3s   %1s   %02X %03d %02X  %02X%02X | %3s  ",
    SharedInfo->proc_parent_pid[pid],
    pid,
    SharedInfo->proc_state[pid] < 7 ? txt_proc_state[SharedInfo->proc_state[pid]] : "?",
    SharedInfo->proc_signal_pending[pid] < 4 ? txt_signal_pending[SharedInfo->proc_signal_pending[pid]] : "?",
    SharedInfo->proc_sp[pid],
    SharedInfo->proc_context[pid],
    SharedInfo->proc_flags[pid],
    SharedInfo->proc_entryH[pid],
    SharedInfo->proc_entryL[pid],
    waitReasonText
  );

  if (waitObjectName != nullptr) {
    Serial1.printf("%3s | ", waitObjectName);
  }
  else {
    Serial1.printf("%02X  | ", waitObject);
  }

  for (uint8_t fd = 0; fd < MAX_FDS; fd++) {
    const uint8_t index = pid * MAX_FDS + fd;
    const uint8_t obj = SharedInfo->proc_fd_obj[index];
    const uint8_t flags = SharedInfo->proc_fd_flags[index];

    if (obj == FD_NONE) {
      Serial1.print("-    ");
      continue;
    }

    Serial1.printf("%d:", obj);

    if (flags & FD_FLAG_READ) {
      Serial1.print("r");
    }

    if (flags & FD_FLAG_WRITE) {
      Serial1.print("w");
    }

    if (pid == SharedInfo->console_owner_pid && fd == STDIN)
      Serial1.print("* ");
    else
      Serial1.print("  ");
  }

  Serial1.println();
}
/// <summary>
/// dump task info for all PIDs, must be compatible with kernel definition
/// </summary>
static void dumpTasks() {
  Serial1.println("PPID PID State Sig SP Ctx Flg Mem  | Wait Obj | FD:");
  for (uint8_t p = 0; p < MAX_PROCS; p++) {
    dumpTask(p);
  }
}

/// <summary>
/// Dump scheduler debug info, compatible with kernel shared debug layout.
/// </summary>
static void dumpSchedDebug() {
  Serial1.println();
  Serial1.println("Scheduler debug:");
  Serial1.println("Path   Marker Current Selected");
  Serial1.printf(
    "%-6s %02X/%-4s %02d      %02d\n",
    dbgPathName(SharedInfo->dbg_sched_path),
    SharedInfo->sched_debug_marker,
    dbgMarkerText(SharedInfo->sched_debug_marker),
    SharedInfo->dbg_sched_current_pid,
    SharedInfo->dbg_sched_selected_pid
  );

  Serial1.println();
  Serial1.println("Save   PID SP Src");
  Serial1.printf(
    "       %02d  %02X %-4s\n",
    SharedInfo->dbg_sched_saved_pid,
    SharedInfo->dbg_sched_saved_sp,
    dbgModeName(SharedInfo->dbg_sched_saved_mode)
  );

  Serial1.println("Load   PID SP");
  Serial1.printf(
    "       %02d  %02X\n",
    SharedInfo->dbg_sched_loaded_pid,
    SharedInfo->dbg_sched_loaded_sp
  );

  Serial1.println("Resume PID Ctx Src");
  Serial1.printf(
    "       %02d  %03d %-4s\n",
    SharedInfo->dbg_sched_resume_pid,
    SharedInfo->dbg_sched_resume_context,
    dbgModeName(SharedInfo->dbg_sched_resume_mode)
  );

  // IRQ preemption debug info, useful for understanding IRQ behavior and debugging IRQ-related issues.
  Serial1.println("\nIRQ preemt:\nCount Current Selected SavedSP LoadSP Skip");
  Serial1.printf(
    "%05d  %02d      %02d       %02X      %02X  %-1s/%02X\n",
    SharedInfo->dbg_irq_preempt_count,
    SharedInfo->dbg_irq_current_pid,
    SharedInfo->dbg_irq_selected_pid,
    SharedInfo->dbg_irq_saved_sp,
    SharedInfo->dbg_irq_loaded_sp,
    SharedInfo->dbg_irq_skip_reason < 7 ? txt_irq_skip_reason[SharedInfo->dbg_irq_skip_reason] : "?",
    SharedInfo->dbg_irq_skip_reason
  );

  Serial1.println();
  Serial1.println("State change:");
  Serial1.println("PID Old     New");
  Serial1.printf(
    "%02d  %-3s/%02X  %-3s/%02X\n",
    SharedInfo->dbg_proc_state_pid,
    SharedInfo->dbg_proc_state_old < 6 ? txt_proc_state[SharedInfo->dbg_proc_state_old] : "???",
    SharedInfo->dbg_proc_state_old,
    SharedInfo->dbg_proc_state_new < 6 ? txt_proc_state[SharedInfo->dbg_proc_state_new] : "???",
    SharedInfo->dbg_proc_state_new
  );
}

/// <summary>
/// dump 6502 interface info, must be compatible with kernel definition
/// </summary>
static void dumpInterface() {
  Serial1.printf("RP_CONSOLE_RDY: %02X\n", snoop_read6502MemoryLoc(RP_CONSOLE_RDY));
  Serial1.printf("RP_STATUS:      %02X\n", snoop_read6502MemoryLoc(RP_STATUS));
  Serial1.printf("RP_ERR:         %02X\n", snoop_read6502MemoryLoc(RP_ERR));
  Serial1.printf("RP_STATE:       %02X\n", snoop_read6502MemoryLoc(RP_STATE));
  Serial1.printf("RP_RES0L:       %02X\n", snoop_read6502MemoryLoc(RP_RES0L));
  Serial1.printf("RP_RES0H:       %02X\n", snoop_read6502MemoryLoc(RP_RES0H));
}

/*
Locks/Gates:
Name     Val Owner Phase Head Tail
SCHED    00  FF    00    00   00
FILE_IO  00  FF    00    FF   FF
PROC     00  FF    00    FF   FF
RP       00  FF
*/
void dumpLockGates() {
  Serial1.println("Locks/Gates:");
  Serial1.println("Name     Val Owner Phase Depth Under");
  Serial1.printf("SCHED    %02X  %02X    %02X    %02X    %02X\n",
    SharedInfo->sched_lock,
    SharedInfo->sched_lock_owner,
    SharedInfo->sched_lock_phase,
    SharedInfo->sched_lock_depth,
    SharedInfo->sched_lock_underflow);
  Serial1.println("\nName     Val Owner Phase Head  Tail");
  Serial1.printf("FILE_IO  %02X  %02X    %02X    %02X    %02X\n",
    SharedInfo->file_io_gate,
    SharedInfo->file_io_gate_owner,
    SharedInfo->file_io_gate_phase,
    SharedInfo->file_io_gate_wait_head,
    SharedInfo->file_io_gate_wait_tail);
  Serial1.printf("PROC     %02X  %02X    %02X    %02X    %02X\n",
    SharedInfo->proc_gate,
    SharedInfo->proc_gate_owner,
    SharedInfo->proc_gate_phase,
    SharedInfo->proc_gate_wait_head,
    SharedInfo->proc_gate_wait_tail);
  Serial1.printf("RP       %02X  %02X\n",
    SharedInfo->rp_lock,
    SharedInfo->rp_lock_owner);
}

/// <summary>
/// dump scheduler info, must be compatible with kernel definition
/// </summary>
void dumpNEOX() {
  snoop_read6502Memory(SHARED_STATE, sizeof(scheduler_info_t), SharedSpace);    // read struct

  switch (SharedInfo->kernel_version) {
  case 0x0203:
    Serial1.println("---------------------------------------\nSystem:");
    Serial1.printf("Sys ticks         = %d\n", (SharedInfo->system_ticks_hi << 8) | SharedInfo->system_ticks_lo);
    Serial1.printf("Active PID        = %d\n", SharedInfo->active_pid);
    Serial1.printf("Active Context    = %d\n", SharedInfo->active_context);
    Serial1.printf("Console Owner PID = %d\n", SharedInfo->console_owner_pid);
    Serial1.printf("Monitor Active    = %d\n", SharedInfo->monitor_active);

    Serial1.println();
    dumpLockGates();

#if 0
    Serial1.println();
    dumpSchedDebug();
#endif

    Serial1.println();
    dumpTasks();

#if 0
    Serial1.println();
    dumpOpenFiles();
#endif

#if 1
    Serial1.println();
    dumpTimers();
#endif

#if 0
    Serial1.println();
    dumpPipes();
#endif

#if 1
    Serial1.println();
    dumpAccounting();
#endif

#if 0
    Serial1.println();
    dumpInterface();
#endif

    Serial1.println();
    break;

  default:
    Serial1.printf("Incompatible kernel version [%04x]\n", SharedInfo->kernel_version);
    break;
  }
}
