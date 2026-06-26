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



// DEBUG BEGIN: temporary RP filesystem local read-test monitor command
/// <summary>
/// cmdFSTestCallback runs the temporary RP-local TEST.TXT/BIG.TXT read
/// validation from the monitor. This is intentionally manual so USB storage and
/// HID keyboard startup remain independent.
/// </summary>
/// <param name="c">SimpleCLI command object.</param>
static void cmdFSTestCallback(cmd* c) {
  Command cmd(c);
  (void)cmd;

  if (!usb_storage_ready()) {
    Serial1.println("*E: fstest: USB storage/FatFs is not ready");
    return;
  }

  Serial1.println("*I: fstest: running RP FS local read test");
  rp_fs_test_read_files();
}
// DEBUG END: temporary RP filesystem local read-test monitor command

/// <summary>
/// help overview of commands
/// </summary>
/// <param name="c"></param>
static void cmdHelpCallback(cmd* c) {
  Serial1.print(F("\nMICmon help:\n\
 > <address> <data>    modify memory address(es)\n\
 clock <freq MHz>      set 6502 clock frequency\n\
 ctx                   show mmu context\n\
 d/is <from> <lines>   disasm memory\n\
 g/o                   go\n\
 help                  help\n\
 irq                   IRQ\n\
 fstest                RP filesystem local read test\n\
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
  // DEBUG END: temporary RP filesystem local read-test monitor command

  gCmd = gCli.addCmd("irq", cmdIRQCallback);

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

static uint8_t gInputIndex = 0;
static char    gInputBuffer[40] = "\0";

/// <summary>
/// return to ICM mode, resetting the input buffer and index, and printing a new prompt for the CLI
/// </summary>
static void returnToICM() {
  gInterface = 0x00;                  // return to ICM
  gInputIndex = 0;
  gInputBuffer[0] = '\0';
  gInMonitor = false;                 // TODO: not correct

  Serial1.printf("%x> ", getMMUContext()); // new prompt for CLI
}

/// <summary>
/// monitorConsoleInput feeds one byte into the console/terminal input path.
/// It performs the same local echo and CPU queue handling as the existing
/// Serial1 terminal path. When allowReturnToICM is false, Ctrl-Z is treated as
/// a normal control byte and is delivered to the selected console instead of
/// returning to the Serial1 monitor.
/// </summary>
/// <param name="c">Input byte.</param>
/// <param name="allowReturnToICM">true for Serial1 terminal mode, false for USB keyboard input.</param>
/// <returns>true if the byte was accepted; false if CPU input queueing failed.</returns>
bool monitorConsoleInput(uint8_t c, bool allowReturnToICM) {
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
      //        Serial1.printf("*D: normalInput: [%s]\n\n", lBuffer);

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
      normalInput(c);
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
        // Parse the user input into the CLI & execute
        gCli.parse(gInputBuffer);
        if (gInterface == 0)
          Serial1.printf("%x> ", getMMUContext());                   // new prompt
        gInputIndex = 0;                         // new buffer
        gInputBuffer[0] = '\0';
        break;

      default:                                   // enter in buffer
        gInputBuffer[gInputIndex++] = c;
        gInputBuffer[gInputIndex] = '\0';
        break;
      }
    }
  }
}
