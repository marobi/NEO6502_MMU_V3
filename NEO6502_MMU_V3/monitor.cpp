/*
This software is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This software is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

*/

//
// simple RPI monitor:
// see help
// 
#include <Arduino.h>
#include <string.h>
#include <SimpleCLI.h>
#include "cmd.h"

#include "config.h"
#include "bios.h"
#include "monitor.h"
#include "control.h"
#include "p6502.h"
#include "mmu.h"
#include "ram.h"
#include "neobus.h"
#include "disasm6502.h"

#include "memory_config.h"

#include "boot.h"
#include "vdu.h"
#include "input.h"

#include "mailbox.h"
#include "scheduler.h"

#include "debug_neox.h"
#include "usb_storage.h"
#include "rp_fs.h"
#include "rp_fs_mailbox.h"
#include "usb_keyboard_layout.h"

// Create CLI Object
static SimpleCLI gCli;
// Commands
static Command gCmd;

static uint8_t gInterface = 0x00;

/// <summary>
/// converts text string hex > integer
/// </summary>
/// <param name="s"></param>
/// <returns></returns>
static int x2i(const char* s) {
  int x = 0;
  for (;;) {
    char c = *s;
    if (c >= '0' && c <= '9') {
      x *= 16;
      x += c - '0';
    }
    else if (c >= 'A' && c <= 'F') {
      x *= 16;
      x += (c - 'A') + 10;
    }
    else if (c >= 'a' && c <= 'f') {
      x *= 16;
      x += (c - 'a') + 10;
    }
    else break;
    s++;
  }
  return x;
}

/// <summary>
/// RESET
/// </summary>
/// <param name="c"></param>
static void cmdResetCallback(cmd* c) {
  Command cmd(c); // Create wrapper object

  gInMonitor = false;
  set6502State(sRESET);
  stopIRQTimer();
  initCmdInterface();
  initMailbox();
  Serial1.println("CPU Reset");
}

/// <summary>
/// GO after halt or reset
/// </summary>
/// <param name="c"></param>
static void cmdGoCallback(cmd* c) {
  Command cmd(c); // Create wrapper object

  set6502State(sRUNNING);
  Serial1.println("CPU Running");
}

/// <summary>
/// SINGLE CYCLE CPU
/// </summary>
/// <param name="c"></param>
static void cmdSCCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("cycles").getValue();
  uint8_t lStep = atoi(arg1.c_str()) & 0xFF;

  singleCycle6502(lStep, true);
}

/// <summary>
/// SINGLE STEP CPU
/// </summary>
/// <param name="c"></param>
static void cmdSSCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("steps").getValue();
  uint8_t lStep = atoi(arg1.c_str()) & 0xFF;

  for (uint8_t s = 0; s < lStep; s++) {
    singleStep6502(false);
    disasm6502(readCPUBusAddress(), 1, false);
  }
}

/// <summary>
/// STOP
/// </summary>
/// <param name="c"></param>
static void cmdStopCallback(cmd* c) {
  Command cmd(c); // Create wrapper object

  set6502State(sHALTED);
  Serial1.println("CPU Halted");
}

/// <summary>
/// DUMP memory
/// </summary>
/// <param name="c"></param>
static void cmdDumpCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("from").getValue();
  String arg2 = cmd.getArgument("to").getValue();
  uint16_t lFrom = x2i(arg1.c_str()) & 0XFFFF;
  uint16_t lTo = max(x2i(arg2.c_str()) & 0XFFFF, lFrom + 15);
  if (lTo <= lFrom) lTo = 0xFFFF;

  Serial1.printf("Dump %04X - %04X\n", lFrom, lTo);

  dumpMemory(lFrom, lTo);
}

/// <summary>
/// DISASM memory
/// </summary>
/// <param name="c"></param>
static void cmdDisAsmCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("from").getValue();
  String arg2 = cmd.getArgument("lines").getValue();
  uint16_t lFrom = x2i(arg1.c_str()) & 0XFFFF;
  uint16_t lLines = atoi(arg2.c_str()) & 0XFF;

  Serial1.printf("Disassembly %04X\n", lFrom);
  disasm6502(lFrom, lLines, false);

  Serial1.println();
}

/// <summary>
/// MEMORY alter
/// </summary>
/// <param name="c"></param>
static void cmdMemCallback(cmd* c) {
  uint16_t lStartAddress, lAddress;
  uint8_t lData;
  Command cmd(c);

  int lCountArgs = cmd.countArgs(); // Get number of arguments
  if (lCountArgs < 2) {
    Serial1.println("*E: not enough parameters\n");
  }
  else {
    lAddress = x2i(cmd.getArgument(0).getValue().c_str()) & 0XFFFF;
    lStartAddress = lAddress;
    // Go through all arguments
    for (uint8_t i = 1; i < lCountArgs; i++) {
      lData = x2i(cmd.getArgument(i).getValue().c_str()) & 0xFF;

      snoop_write6502Memory(lAddress++, 1, &lData);
    }

    // just nice
    Serial1.printf("Dump %04X - %04X\n", lStartAddress, --lAddress);
    dumpMemory(lStartAddress, lAddress);
  }
}

/// <summary>
/// STATUS
/// </summary>
/// <param name="c"></param>
static void cmdStatusCallback(cmd* c) {
  Serial1.printf("Status\n");
  show6502State();
  Serial1.printf("*I: CTX: %02X\n", getMMUContext());
}

/// <summary>
/// MMU context change
/// </summary>
/// <param name="c"></param>
static void cmdMMUCallback(cmd* c) {
  Command cmd(c);
  //  String arg1 = cmd.getArgument("context").getValue();

  //  uint8_t lContext = x2i(arg1.c_str()) & 0x7F;  // 128

  //  if (lContext == 127)
  uint8_t lContext = getMMUContext();

  Serial1.printf("MMU: %02X\n", lContext);

  sysstate_t lState = get6502State();
  set6502State(sRPI);

  dumpMMUPageMap(lContext);   // dump context

  set6502State(lState);
}

/// <summary>
/// MMU context index page change
/// </summary>
/// <param name="c"></param>
static void cmdPageCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("index").getValue();
  String arg2 = cmd.getArgument("page").getValue();
  uint8_t lContext = getMMUContext();
  uint8_t lIndex = x2i(arg1.c_str()) & 0xFF;
  uint8_t lPage = x2i(arg2.c_str()) & 0xFF;

  sysstate_t lState = get6502State();
  set6502State(sRPI);
  uint8_t lPrevPage = readMMUPage(lContext, lIndex);
  writeMMUPage(lContext, lIndex, lPage);
  set6502State(lState);

  Serial1.printf("MMU page: %02X:%02X %02X => %02X\n", lContext, lIndex, lPrevPage, lPage);
}

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
static void cmdCommandCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("PID").getValue();
  uint8_t lPID = atoi(arg1.c_str()) & 0xFF;

  if ((lPID > 0) && (gInMonitor)) {
    Serial1.println("*E: cmdCommandCallback: already in monitor!");
    return;
  }

  Serial1.printf("*I: Set console PID to %d\n", lPID);

  setConsolePID(lPID);
  if (lPID == 0) {
    gInMonitor = true;
  }

  gInterface++;
  Serial1.printf("*I: Entering console [%d]\n", lPID);
}

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
static void cmdSysConfigCallback(cmd* c) {
  Command cmd(c);

  bootSystemWithMenu();
}

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
static void cmdZeroCallback(cmd* c) {
  Command cmd(c);

  sysstate_t save_state = get6502State();

  set6502State(sBOOT);
  fillMemory(0x00);
  set6502State(save_state);

  Serial1.println("Memory zeroed");
}

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
static void cmdClockCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("freq").getValue();

  uint32_t lFreq = atof(arg1.c_str()) * MHZ;

  if (lFreq > 0.001)
    set6502Clockfrequency(lFreq);

  Serial1.printf("*I: Clock = %2.1f MHz\n", (float)get6502ClockFrequency() / MHZ);
}

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
static void cmdIRQCallback(cmd* c) {
  Command cmd(c);

  Serial1.println("*I: IRQ ...");
  if (!genIRQ6502(RP_SRC_TIMER)) {
    Serial1.println("*E: cmdIRQCallback: IRQ gen failed");
  }
}

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
static void cmdTimerCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("freq").getValue();

  unsigned long lInterval = 1000 / atof(arg1.c_str());

  if (lInterval < 1) {
    stopIRQTimer();
  }
  else {
    startIRQTimer(lInterval);
  }
}

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
static void cmdMonitorCallback(cmd* c) {
  Command cmd(c);

  Serial1.println("*I: Monitor ...");
//  stopIRQTimer();
  if (! genIRQ6502(RP_SRC_MONITOR)) {
    Serial1.println("*E: cmdIRQCallback: Monitor entry failed");
  }
}

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
static void cmdInfoCallback(cmd* c) {
  Command cmd(c);

  dumpNEOX();
}

/// <summary>
/// cmdKeymapCallback shows or changes the active USB keyboard locale.
/// Supported locales are US and DE. The mapping remains ASCII-safe.
/// </summary>
/// <param name="c">SimpleCLI command object.</param>
static void cmdKeymapCallback(cmd* c) {
  Command cmd(c);
  String locale = cmd.getArgument("locale").getValue();
  locale.trim();
  locale.toLowerCase();

  if (locale.length() == 0 || locale == "?") {
    Serial1.printf("*I: USB keyboard layout: %s\n", usb_keyboard_get_locale_name());
    return;
  }

  if (locale == "us") {
    usb_keyboard_set_locale(USB_KEYBOARD_LOCALE_US);
    Serial1.printf("*I: USB keyboard layout: %s\n", usb_keyboard_get_locale_name());
    return;
  }

  if (locale == "de") {
    usb_keyboard_set_locale(USB_KEYBOARD_LOCALE_DE);
    Serial1.printf("*I: USB keyboard layout: %s\n", usb_keyboard_get_locale_name());
    return;
  }

  Serial1.println("*E: keymap: use us or de");
}


/// <summary>
/// cmdUSBDisksCallback prints the current USB MSC slot/FatFs state.
/// </summary>
/// <param name="c">SimpleCLI command object.</param>
static void cmdUSBDisksCallback(cmd* c) {
  Command cmd(c);
  (void)cmd;

  usb_storage_print_disks();
}

// DEBUG BEGIN: temporary RP filesystem local read-test monitor command
/// <summary>
/// cmdFSTestCallback runs the temporary RP-local TEST.TXT/BIG.TXT read
/// validation from the monitor. This is intentionally manual so USB storage and
/// HID keyboard startup remain independent.
/// </summary>
/// <param name="c">SimpleCLI command object.</param>
static void cmdFSTestCallback(cmd* c) {
  Command cmd(c);
  String device_arg = cmd.getArgument("device").getValue();
  device_arg.trim();

  uint8_t device = 0;
  if (device_arg.length() > 0) {
    for (uint16_t i = 0; i < device_arg.length(); i++) {
      if (!isDigit(device_arg[i])) {
        Serial1.println("*E: fstest: device must be 0..3");
        return;
      }
    }

    long const parsed = device_arg.toInt();
    if (parsed < 0 || parsed >= USB_STORAGE_MAX_DEVICES) {
      Serial1.println("*E: fstest: device must be 0..3");
      return;
    }
    device = (uint8_t)parsed;
  }

  if (!usb_storage_ready(device)) {
    Serial1.printf("*E: fstest: USB storage/FatFs device %u is not ready\n", (unsigned)device);
    return;
  }

  Serial1.printf("*I: fstest: running RP FS local read test on device %u drive %u:\n",
                 (unsigned)device,
                 (unsigned)device);
  rp_fs_test_read_files(device);
}
// DEBUG END: temporary RP filesystem local read-test monitor command


/// <summary>
/// ls_parse_u8 parses a decimal monitor argument and validates it against an
/// inclusive upper bound.
/// </summary>
/// <param name="arg">Argument text.</param>
/// <param name="max_value">Maximum accepted value.</param>
/// <param name="value">Parsed output.</param>
/// <returns>true when the value is valid.</returns>
static bool ls_parse_u8(String arg, uint8_t max_value, uint8_t* value) {
  if (value == nullptr)
    return false;

  arg.trim();
  if (arg.length() == 0)
    return false;

  for (uint16_t i = 0; i < arg.length(); i++) {
    if (!isDigit(arg[i]))
      return false;
  }

  long const parsed = arg.toInt();
  if (parsed < 0 || parsed > max_value)
    return false;

  *value = (uint8_t)parsed;
  return true;
}

/// <summary>
/// cmdLSCallback lists one explicit directory path using the RP V35 directory
/// handle API directly. It is a permanent RP monitor command and does not use
/// NEOX syscalls or mailbox READDIR.
/// </summary>
/// <param name="c">SimpleCLI command object.</param>
static void cmdLSCallback(cmd* c) {
  Command cmd(c);

  uint8_t device = 0;
  if (!ls_parse_u8(cmd.getArgument("device").getValue(), USB_STORAGE_MAX_DEVICES - 1, &device)) {
    Serial1.println("*E: ls: device must be 0..3");
    return;
  }

  String path = cmd.getArgument("path").getValue();
  path.trim();
  if (path.length() == 0)
    path = "/";

  if (!usb_storage_ready(device)) {
    Serial1.printf("*E: ls: USB storage/FatFs device %u is not ready\n", (unsigned)device);
    return;
  }

  Serial1.printf("*I: ls: device=%u path=%s\n", (unsigned)device, path.c_str());

  int const dir_handle = rp_fs_opendir_83(device, path.c_str());
  if (dir_handle < 0) {
    Serial1.printf("*E: ls: opendir failed device=%u path=%s\n", (unsigned)device, path.c_str());
    return;
  }

  uint16_t count = 0;
  for (;;) {
    char name[13];
    uint8_t attr = 0;
    uint32_t size = 0;

    int const result = rp_fs_readdir((uint8_t)dir_handle, name, sizeof(name), &attr, &size);
    if (result < 0) {
      Serial1.printf("*E: ls: readdir failed handle=%u after %u entries\n", (unsigned)dir_handle, (unsigned)count);
      (void)rp_fs_closedir((uint8_t)dir_handle);
      return;
    }

    if (result == 0)
      break;

    char const type_char = ((attr & 0x10) != 0) ? 'd' : '-';
    Serial1.printf("%c %10lu  %s\n",
                   type_char,
                   (unsigned long)size,
                   name);
    count++;
  }

  if (!rp_fs_closedir((uint8_t)dir_handle)) {
    Serial1.printf("*E: ls: closedir failed handle=%u\n", (unsigned)dir_handle);
    return;
  }

  Serial1.printf("*I: ls: OK entries=%u\n", (unsigned)count);
}

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
static void cmdBootCallback(cmd* c) {
  Command cmd(c);

  Serial1.println("*I: Booting NEOX ...");
  set6502State(sRUNNING);
  set6502Clockfrequency(8 * MHZ);
  startIRQTimer(10);
}

// DEBUG BEGIN: temporary RP V31 bulk filesystem monitor command
static constexpr uint16_t FSBULK_ARG_ADDR = 0x0200;
static constexpr uint16_t FSBULK_PATH_ADDR = 0x0210;
static constexpr uint16_t FSBULK_SRC_ADDR = 0x0300;
static constexpr uint16_t FSBULK_DST_ADDR = 0x0380;
static constexpr char FSBULK_FILENAME[] = "BULKTEST.TXT";
static constexpr char FSBULK_TEXT[] = "NEOX V31 BULK TEST";

/// <summary>
/// fsbulk_parse_u8 parses a decimal monitor argument and validates it against
/// an inclusive upper bound.
/// </summary>
/// <param name="arg">Argument text.</param>
/// <param name="max_value">Maximum accepted value.</param>
/// <param name="value">Parsed output.</param>
/// <returns>true when the value is valid.</returns>
static bool fsbulk_parse_u8(String arg, uint8_t max_value, uint8_t* value) {
  if (value == nullptr)
    return false;

  arg.trim();
  if (arg.length() == 0)
    return false;

  for (uint16_t i = 0; i < arg.length(); i++) {
    if (!isDigit(arg[i]))
      return false;
  }

  long const parsed = arg.toInt();
  if (parsed < 0 || parsed > max_value)
    return false;

  *value = (uint8_t)parsed;
  return true;
}

/// <summary>
/// fsbulk_begin_context selects a monitor test context and returns the previous
/// CPU state/context so it can be restored. This is used only to prepare and
/// inspect the 6502-side test buffers for the temporary monitor test.
/// </summary>
/// <param name="context">MMU context to select.</param>
/// <param name="saved_state">Previous CPU/RP bus state.</param>
/// <param name="saved_context">Previous MMU context.</param>
/// <returns>true when the context is valid.</returns>
static bool fsbulk_begin_context(uint8_t context, sysstate_t* saved_state, uint8_t* saved_context) {
  if (saved_state == nullptr || saved_context == nullptr || context >= MAX_MEMORY_CONTEXTS)
    return false;

  *saved_state = get6502State();
  set6502State(sRPI);
  *saved_context = getMMUContext();

  if (*saved_context != context)
    setMMUContext(context);

  return true;
}

/// <summary>
/// fsbulk_end_context restores CPU state and MMU context saved by
/// fsbulk_begin_context.
/// </summary>
/// <param name="saved_state">Previous CPU/RP bus state.</param>
/// <param name="saved_context">Previous MMU context.</param>
static void fsbulk_end_context(sysstate_t saved_state, uint8_t saved_context) {
  if (getMMUContext() != saved_context)
    setMMUContext(saved_context);

  set6502State(saved_state);
}

/// <summary>
/// fsbulk_write16 writes a little-endian 16-bit value into the currently
/// selected monitor test context.
/// </summary>
/// <param name="addr">6502 address.</param>
/// <param name="value">Value to write.</param>
static void fsbulk_write16(uint16_t addr, uint16_t value) {
  write6502Memory(addr, (uint8_t)(value & 0xFF));
  write6502Memory((uint16_t)(addr + 1), (uint8_t)((value >> 8) & 0xFF));
}

/// <summary>
/// fsbulk_prepare_context builds the 6502-side path, data, destination buffer,
/// and 8-byte bulk argument block in the chosen context.
/// </summary>
/// <param name="context">MMU context used for the test buffers.</param>
/// <param name="device">USB/FatFs device number.</param>
/// <param name="save_args">true for SAVE args, false for LOAD args.</param>
/// <param name="byte_count">SAVE byte_count or LOAD max_bytes.</param>
/// <returns>true when the test context was prepared.</returns>
static bool fsbulk_prepare_context(uint8_t context, uint8_t device, bool save_args, uint16_t byte_count) {
  sysstate_t saved_state;
  uint8_t saved_context;
  if (!fsbulk_begin_context(context, &saved_state, &saved_context))
    return false;

  for (uint16_t i = 0; i < sizeof(FSBULK_FILENAME); i++)
    write6502Memory((uint16_t)(FSBULK_PATH_ADDR + i), (uint8_t)FSBULK_FILENAME[i]);

  for (uint16_t i = 0; i < sizeof(FSBULK_TEXT) - 1; i++)
    write6502Memory((uint16_t)(FSBULK_SRC_ADDR + i), (uint8_t)FSBULK_TEXT[i]);

  for (uint16_t i = 0; i < 64; i++)
    write6502Memory((uint16_t)(FSBULK_DST_ADDR + i), 0);

  fsbulk_write16((uint16_t)(FSBULK_ARG_ADDR + 0), FSBULK_PATH_ADDR);
  fsbulk_write16((uint16_t)(FSBULK_ARG_ADDR + 2), save_args ? FSBULK_SRC_ADDR : FSBULK_DST_ADDR);
  fsbulk_write16((uint16_t)(FSBULK_ARG_ADDR + 4), byte_count);
  write6502Memory((uint16_t)(FSBULK_ARG_ADDR + 6), device);
  write6502Memory((uint16_t)(FSBULK_ARG_ADDR + 7), 0);

  fsbulk_end_context(saved_state, saved_context);
  return true;
}

/// <summary>
/// fsbulk_compare_context compares the loaded destination buffer against the
/// source test string in the selected context.
/// </summary>
/// <param name="context">MMU context containing the test buffers.</param>
/// <returns>true when the loaded bytes match.</returns>
static bool fsbulk_compare_context(uint8_t context) {
  sysstate_t saved_state;
  uint8_t saved_context;
  if (!fsbulk_begin_context(context, &saved_state, &saved_context))
    return false;

  bool ok = true;
  for (uint16_t i = 0; i < sizeof(FSBULK_TEXT) - 1; i++) {
    uint8_t const value = read6502Memory((uint16_t)(FSBULK_DST_ADDR + i));
    if (value != (uint8_t)FSBULK_TEXT[i]) {
      ok = false;
      break;
    }
  }

  fsbulk_end_context(saved_state, saved_context);
  return ok;
}

/// <summary>
/// Prepares a monitor-side direct call to the same generic filesystem handler
/// used by NEOX. PID 0 suppresses completion IRQ generation because the monitor
/// invokes the handler synchronously.
/// </summary>
/// <param name="operation">RP_FS_OP_LOAD or RP_FS_OP_SAVE.</param>
/// <param name="context">Trusted caller context placed in ARG1H.</param>
static void fsbulk_prepare_mailbox(uint8_t operation, uint8_t context) {
  rp_mailbox_clear_result_fields();
  snoop_write6502MemoryLoc(RP_GROUP, RP_GROUP_FS);
  snoop_write6502MemoryLoc(RP_CMD, RP_FS_CMD_EXEC);
  snoop_write6502MemoryLoc(RP_STATE, operation);
  snoop_write6502MemoryLoc(RP_STATUS, RP_BUSY);
  rp_write16(RP_ARG0L, FSBULK_ARG_ADDR);
  snoop_write6502MemoryLoc(RP_ARG1L, 0);
  snoop_write6502MemoryLoc(RP_ARG1H, context);
  rp_write16(RP_ARG2L, 0);
}

/// <summary>
/// fsbulk_print_result prints the current mailbox result after a direct monitor
/// call to a bulk filesystem handler.
/// </summary>
/// <param name="phase">SAVE or LOAD phase label.</param>
/// <returns>true when RP_STATUS is RP_DONE.</returns>
static bool fsbulk_print_result(const char* phase) {
  uint8_t const status = snoop_read6502MemoryLoc(RP_STATUS);
  uint8_t const err = snoop_read6502MemoryLoc(RP_ERR);
  uint16_t const res0 = rp_read16(RP_RES0L);
  uint8_t const flags = snoop_read6502MemoryLoc(RP_FLAGS);

  if (status == RP_DONE) {
    Serial1.printf("*I: fsbulk: %s OK bytes=%u flags=%02X\n", phase, (unsigned)res0, (unsigned)flags);
    return true;
  }

  Serial1.printf("*E: fsbulk: %s failed status=%02X err=%02X res0=%04X flags=%02X\n",
                 phase,
                 (unsigned)status,
                 (unsigned)err,
                 (unsigned)res0,
                 (unsigned)flags);
  return false;
}

/// <summary>
/// cmdFSBulkCallback runs a temporary V31 RP-side bulk SAVE/LOAD monitor test.
/// It writes a fixed string from the selected context to BULKTEST.TXT on the
/// chosen USB/FatFs device, loads the file back into a second buffer in the same
/// context, and compares the result.
/// </summary>
/// <param name="c">SimpleCLI command object.</param>
static void cmdFSBulkCallback(cmd* c) {
  Command cmd(c);

  uint8_t device = 0;
  if (!fsbulk_parse_u8(cmd.getArgument("device").getValue(), USB_STORAGE_MAX_DEVICES - 1, &device)) {
    Serial1.println("*E: fsbulk: device must be 0..3");
    return;
  }

  uint8_t context = getMMUContext();
  String context_arg = cmd.getArgument("context").getValue();
  context_arg.trim();
  if (context_arg.length() > 0) {
    if (!fsbulk_parse_u8(context_arg, MAX_MEMORY_CONTEXTS - 1, &context)) {
      Serial1.println("*E: fsbulk: invalid context");
      return;
    }
  }

  if (!usb_storage_ready(device)) {
    Serial1.printf("*E: fsbulk: USB storage/FatFs device %u is not ready\n", (unsigned)device);
    return;
  }

  Serial1.printf("*I: fsbulk: device=%u context=%u file=%s\n",
                 (unsigned)device,
                 (unsigned)context,
                 FSBULK_FILENAME);

  uint16_t const test_len = (uint16_t)(sizeof(FSBULK_TEXT) - 1);

  if (!fsbulk_prepare_context(context, device, true, test_len)) {
    Serial1.println("*E: fsbulk: failed to prepare SAVE context");
    return;
  }

  fsbulk_prepare_mailbox(RP_FS_OP_SAVE, context);
  (void)rp_fs_mailbox_handle_exec();
  if (!fsbulk_print_result("SAVE")) {
    snoop_write6502MemoryLoc(RP_STATUS, RP_IDLE);
    return;
  }

  if (!fsbulk_prepare_context(context, device, false, 64)) {
    Serial1.println("*E: fsbulk: failed to prepare LOAD context");
    snoop_write6502MemoryLoc(RP_STATUS, RP_IDLE);
    return;
  }

  fsbulk_prepare_mailbox(RP_FS_OP_LOAD, context);
  (void)rp_fs_mailbox_handle_exec();
  if (!fsbulk_print_result("LOAD")) {
    snoop_write6502MemoryLoc(RP_STATUS, RP_IDLE);
    return;
  }

  if (!fsbulk_compare_context(context)) {
    Serial1.println("*E: fsbulk: compare failed");
    snoop_write6502MemoryLoc(RP_STATUS, RP_IDLE);
    return;
  }

  Serial1.println("*I: fsbulk: compare OK");
  snoop_write6502MemoryLoc(RP_STATUS, RP_IDLE);
}
// DEBUG END: temporary RP V31 bulk filesystem monitor command


/// <summary>
/// help overview of commands
/// </summary>
/// <param name="c"></param>
static void cmdHelpCallback(cmd* c) {
  Serial1.print(F("\nMICmon help:\n\
 > <address> <data>    modify memory address(es)\n\
 b/oot                 boot NEOX\n\
 clock <freq MHz>      set 6502 clock frequency\n\
 ctx                   show mmu context\n\
 d/is <from> <lines>   disasm memory\n\
 g/o                   go\n\
 help                  help\n\
 irq                   IRQ\n\
 fstest [device]       RP filesystem local read test, default device 0\n\
 fsbulk [dev] [ctx]    temporary V31 bulk save/load test\n\
 keymap [us|de]        show/set USB keyboard layout\n\
 ls [dev] [path]       list directory using RP filesystem\n\
 usbdisks              show USB MSC slots/FatFs drives\n\
 m/em <from> <to>      dump memory\n\
 mon/itor              enter monitor\n\
 page <index> <page>   set mmu page\n\
 ps                    dump scheduler info\n\
 res/et                reset\n\
 s/top                 stop\n\
 sc <cycles>           single cycle\n\
 ss <steps>            single step\n\
 st/at                 status of cpus/bus\n\
 syscfg                system configuration\n\
 t/erm                 terminal mode\n\
 timer <freq>          IRQ timer\n\
 zero                  zero memory\n\
\n"));
}

/// <summary>
/// CLI error handler
/// </summary>
/// <param name="e"></param>
static void errorCallback(cmd_error* e) {
  CommandError cmdError(e); // Create wrapper object

  Serial1.printf("*E: MIC-ICM: %s\n", cmdError.toString());

  // Print command usage
  if (cmdError.hasCommand()) {
    Serial1.print("Did you mean \"");
    Serial1.print(cmdError.getCommand().toString());
    Serial1.println("\"?");
  }
}

/// <summary>
/// init the monitor commands
/// </summary>
void initMonitor() {
  Serial1.printf("\nMIC-ICM (%s) %s\n%x> ", BIOS_CPU, MON_VERSION, getMMUContext());

  gCmd = gCli.addCmd("b/oot", cmdBootCallback);

  // Create the commands with callback function
  gCmd = gCli.addCmd("clock", cmdClockCallback);
  gCmd.addPositionalArgument("freq", "0");

  gCmd = gCli.addCmd("ctx", cmdMMUCallback);

  gCmd = gCli.addCmd("d/is", cmdDisAsmCallback);
  gCmd.addPositionalArgument("from");
  gCmd.addPositionalArgument("lines", "1");

  gCmd = gCli.addCmd("g/o", cmdGoCallback);

  gCmd = gCli.addCmd("h/elp", cmdHelpCallback);

  // DEBUG BEGIN: temporary RP filesystem local read-test monitor command
  gCmd = gCli.addCmd("fstest", cmdFSTestCallback);
  gCmd.addPositionalArgument("device", "0");
  // DEBUG END: temporary RP filesystem local read-test monitor command

  // DEBUG BEGIN: temporary RP V31 bulk filesystem monitor command
  gCmd = gCli.addCmd("fsbulk", cmdFSBulkCallback);
  gCmd.addPositionalArgument("device", "0");
  gCmd.addPositionalArgument("context", "");
  // DEBUG END: temporary RP V31 bulk filesystem monitor command

  gCmd = gCli.addCmd("irq", cmdIRQCallback);

  gCmd = gCli.addCmd("keymap", cmdKeymapCallback);
  gCmd.addPositionalArgument("locale", "");

    gCmd = gCli.addCmd("ls", cmdLSCallback);
  gCmd.addPositionalArgument("device", "0");
  gCmd.addPositionalArgument("path", "/");
  
  gCmd = gCli.addCmd("usbdisks", cmdUSBDisksCallback);

  gCmd = gCli.addBoundlessCommand(">", cmdMemCallback);

  gCmd = gCli.addCmd("m/em", cmdDumpCallback);
  gCmd.addPositionalArgument("from", "0");
  gCmd.addPositionalArgument("to", "0");

  gCmd = gCli.addCmd("mon/itor", cmdMonitorCallback);

  gCmd = gCli.addCmd("ps", cmdInfoCallback);

  gCmd = gCli.addCmd("r/eset", cmdResetCallback);

  gCmd = gCli.addCmd("sc", cmdSCCallback);
  gCmd.addPositionalArgument("cycles", "1");

  gCmd = gCli.addCmd("ss", cmdSSCallback);
  gCmd.addPositionalArgument("steps", "1");

  gCmd = gCli.addCmd("st/at", cmdStatusCallback);

  gCmd = gCli.addCmd("syscfg", cmdSysConfigCallback);

  gCmd = gCli.addCmd("s/top", cmdStopCallback);

  gCmd = gCli.addCmd("t/erm", cmdCommandCallback);
  gCmd.addPositionalArgument("PID", "0");

  gCmd = gCli.addCmd("timer", cmdTimerCallback);
  gCmd.addPositionalArgument("freq", "0");




  gCmd = gCli.addCmd("page", cmdPageCallback);
  gCmd.addPositionalArgument("index");
  gCmd.addPositionalArgument("page");

  gCmd = gCli.addCmd("zero", cmdZeroCallback);

  // Set error Callback
  gCli.setOnError(errorCallback);
}

////////////////////////////////////////////////////////////////////////////

static constexpr size_t ICM_INPUT_BUFFER_SIZE = 40;

static uint8_t gInputIndex = 0;
static char gInputBuffer[ICM_INPUT_BUFFER_SIZE] = { 0 };
static bool gInputOverflow = false;

/// <summary>
/// Resets the Serial1 monitor command-line buffer to an empty state.
/// </summary>
static void resetMonitorInput() {
  gInputIndex = 0;
  gInputBuffer[0] = '\0';
  gInputOverflow = false;
}

/// <summary>
/// Appends one printable ASCII byte to the monitor command-line buffer.
/// An overlong line is discarded through its terminating newline so probe
/// reconnect noise cannot overwrite adjacent RP memory or become a command.
/// </summary>
/// <param name="c">Printable ASCII byte to append.</param>
/// <returns>true when appended; false when ignored or the line overflowed.</returns>
static bool appendMonitorInput(const int c) {
  if (gInputOverflow)
    return false;

  if ((c < 0x20) || (c > 0x7E))
    return false;

  if (gInputIndex >= (ICM_INPUT_BUFFER_SIZE - 1)) {
    gInputIndex = 0;
    gInputBuffer[0] = '\0';
    gInputOverflow = true;
    return false;
  }

  gInputBuffer[gInputIndex++] = static_cast<char>(c);
  gInputBuffer[gInputIndex] = '\0';
  return true;
}

/// <summary>
/// Checks whether a byte received through the Debug Probe UART is safe to
/// forward to the active terminal. NUL, framing-error values and high-bit
/// reconnect garbage are discarded; normal 7-bit terminal controls remain valid.
/// </summary>
/// <param name="c">Value returned by Serial1.read().</param>
/// <returns>true for a valid non-NUL 7-bit terminal byte.</returns>
static bool isValidProbeTerminalInput(const int c) {
  return (c > 0x00) && (c <= 0x7F);
}

/// <summary>
/// return to ICM mode, resetting the input buffer and index, and printing a new prompt for the CLI
/// </summary>
static void returnToICM() {
  gInterface = 0x00;                  // return to ICM
  resetMonitorInput();
  gInMonitor = false;                 // TODO: not correct

  Serial1.printf("%x> ", getMMUContext()); // new prompt for CLI
}

/// <summary>
/// monitorConsoleInput feeds one byte into the console/terminal input path.
/// It performs the same local echo and CPU queue handling as the existing
/// Serial1 terminal path. Ctrl-C is always consumed as an out-of-band NEOX
/// console-break request. When allowReturnToICM is false, Ctrl-Z is treated as
/// a normal control byte and is delivered to the selected console instead of
/// returning to the Serial1 monitor.
/// </summary>
/// <param name="c">Input byte.</param>
/// <param name="allowReturnToICM">true for Serial1 terminal mode, false for USB keyboard input.</param>
/// <returns>true if the byte was accepted; false if CPU input queueing failed.</returns>
bool monitorConsoleInput(uint8_t c, bool allowReturnToICM) {
  // The RP/Serial terminal supplies already translated character bytes.
  // Ctrl-C is therefore fixed at ASCII control byte $03 on this path.
  // Consume it before local echo, VDU control processing, or CPU queueing.
  if (c == ctrl('C')) {
    requestConsoleBreakIRQ();
    return true;
  }

  if (c == ctrl('Z') && allowReturnToICM) { // ^Z returns only Serial1 terminal mode to ICM
    returnToICM();
    return true;
  }

  if ((c == '\n') || (c == '\r')) {
    uint8_t lBuffer[COLS + 1];
    uint8_t* lPtr = lBuffer;

    writeVDUQ('\r');

    if (getAsScreenMode()) {
      vduGetCurrentScreenline(lBuffer);
//      Serial1.printf("*D: normalInput: [%s]\n\n", lBuffer);

      vduRestoreCursor();

      while (*lPtr) {
        if (!writeCPUQ(*lPtr++)) {
          if (allowReturnToICM)
            returnToICM();
          return false;
        }
      }

      if (!writeCPUQ('\r')) {            // go
        if (allowReturnToICM)
          returnToICM();
        return false;
      }
    }
  }
  else {
    //      Serial1.printf("*D: ICM->VDU: [%02X]\n", c);
    writeVDUQ(c);
  }

  if (!getAsScreenMode()) {              // normal mode
    if (!writeCPUQ(c)) {
      if (allowReturnToICM)
        returnToICM();
      return false;
    }
  }

  return true;
}

/// <summary>
/// normalInput is the Serial1 terminal input wrapper. It keeps the legacy
/// Ctrl-Z return-to-ICM behavior for PicoProbe/debug use only.
/// </summary>
/// <param name="c"></param>
static void normalInput(uint8_t c) {
  (void)monitorConsoleInput(c, true);
}

/// <summary>
/// rpi monitor to control the HW
/// </summary>
/// <returns>void</returns>
void taskICMonitor() {
  int c;
  //  uint8_t cnt;

    // Check if user typed something on keyboard
  if (Serial1.available()) {
    c = Serial1.read();

    if (gInterface != 0) {
      if (isValidProbeTerminalInput(c))
        normalInput(static_cast<uint8_t>(c));
    }
    else {
      switch (c) {
      case -1:                                   // no char
        break;

      case '\b':                                 // backspace
      case 127:                                  // delete
        if (gInputIndex >= 1) {
          gInputIndex--;                         // delete last char
          gInputBuffer[gInputIndex] = '\0';      // from buffer
          Serial1.print(" \b");
        }
        else
          Serial1.print(" ");                     // buffer is empty
        break;

      case '\r':                                 // LF
        break;

      case '\n':                                 // CR
        // Parse only complete, bounded command lines. Overlong lines are
        // discarded because they may be reconnect noise from the Debug Probe.
        if (!gInputOverflow)
          gCli.parse(gInputBuffer);
        if (gInterface == 0)
          Serial1.printf("%x> ", getMMUContext());                   // new prompt
        resetMonitorInput();
        break;

      default:                                   // enter in buffer
        appendMonitorInput(c);
        break;
      }
    }
  }
}
