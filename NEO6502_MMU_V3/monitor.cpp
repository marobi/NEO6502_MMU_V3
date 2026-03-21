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
int x2i(const char* s)
{
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

  set6502State(sRESET);
  inpInit();
  initCmdInterface();
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
  Serial1.printf("*I: CTX: %02X\n", readMMUContext());
}

/// <summary>
/// IRQ
/// </summary>
/// <param name="c"></param>
static void cmdIRQCallback(cmd* c) {
  Serial1.printf("IRQ\n");
}

/// <summary>
/// MMU context change
/// </summary>
/// <param name="c"></param>
static void cmdMMUCallback(cmd* c) {
  Command cmd(c);
  String arg1 = cmd.getArgument("context").getValue();

  uint8_t lContext = x2i(arg1.c_str()) & 0x7F;  // 128

 // Serial1.printf("MMU: %02X\n", lContext);

  uint8_t lState = get6502State();
  set6502State(sRPI);
  
  writeMMUContext(lContext);  // set the context
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
  uint8_t lContext = readMMUContext();
  uint8_t lIndex = x2i(arg1.c_str()) & 0xFF;
  uint8_t lPage = x2i(arg2.c_str()) & 0xFF;

  uint8_t lState = get6502State();
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

  gInterface++;
  Serial1.println("Entering terminal ...");
}

static void cmdSysConfigCallback(cmd* c) {
  Command cmd(c);

  bootSystemWithMenu();
}

static void cmdZeroCallback(cmd* c) {
  Command cmd(c);

  uint8_t save_state = get6502State();

  set6502State(sBOOT);
  fillMemory(0x00);
  set6502State(save_state);

  Serial1.println("Memory zeroed");
}

static void cmdClockCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("freq").getValue();

  uint32_t lFreq = atof(arg1.c_str()) * MEGA_HZ;

  if (lFreq < 0.001 * MEGA_HZ)
    lFreq = DEFAULT_6502_CLOCK;

  set6502Clockfrequency(lFreq);

  Serial1.printf("*I: Clock = %4.2f MHz\n", (float)lFreq / MEGA_HZ);
}

/// <summary>
/// help overview of commands
/// </summary>
/// <param name="c"></param>
static void cmdHelpCallback(cmd* c) {
  Serial1.print("\nMICmon help:\n\
 > <address> <data>    modify memory address(es)\n\
 clock <freq MHz>      set 6502 clock frequency\n\
 d/is <from> <lines>   disasm memory\n\
 g/o                   go\n\
 help                  help\n\
 i/rq                  generate IRQ\n\
 m/em <from> <to>      dump memory\n\
 mmu  <context>        set mmu context\n\
 page <index> <page>   set mmu page\n\
 r/eset                reset\n\
 s/top                 stop\n\
 sc <cycles>           single cycle\n\
 ss <steps>            single step\n\
 st/at                 status of cpus/bus\n\
 syscfg                system configuration\n\
 t/erm                 terminal mode\n\
 zero                  zero memory\n\
\n");
}

/// <summary>
/// CLI error handler
/// </summary>
/// <param name="e"></param>
static void errorCallback(cmd_error* e) {
  CommandError cmdError(e); // Create wrapper object

  Serial1.printf("*E: MIC-ICM: %s\n", cmdError.toString());

  if (cmdError.hasCommand()) {
    Serial1.printf("*E: Did you mean [%s]?\n", cmdError.getCommand().toString());
  }
}

/// <summary>
/// init the monitor commands
/// </summary>
void initMonitor() {
  Serial1.printf("\nMIC-ICM (%s) %s\n> ", BIOS_CPU, MON_VERSION);

  // Create the commands with callback function
  gCmd = gCli.addCmd("clock", cmdClockCallback);
  gCmd.addPositionalArgument("freq");

  gCmd = gCli.addCmd("d/is", cmdDisAsmCallback);
  gCmd.addPositionalArgument("from");
  gCmd.addPositionalArgument("lines", "1");

  gCmd = gCli.addCmd("m/em", cmdDumpCallback);
  gCmd.addPositionalArgument("from");
  gCmd.addPositionalArgument("to", "0");

  gCmd = gCli.addCmd("g/o", cmdGoCallback);

  gCmd = gCli.addCmd("h/elp", cmdHelpCallback);

  gCmd = gCli.addCmd("i/rq", cmdIRQCallback);

  gCmd = gCli.addBoundlessCommand(">", cmdMemCallback);

  gCmd = gCli.addCmd("mmu", cmdMMUCallback);
  gCmd.addPositionalArgument("context", "0");

  gCmd = gCli.addCmd("r/eset", cmdResetCallback);

  gCmd = gCli.addCmd("sc", cmdSCCallback);
  gCmd.addPositionalArgument("cycles", "1");

  gCmd = gCli.addCmd("ss", cmdSSCallback);
  gCmd.addPositionalArgument("steps", "1");

  gCmd = gCli.addCmd("s/top", cmdStopCallback);

  gCmd = gCli.addCmd("st/at", cmdStatusCallback);

  gCmd = gCli.addCmd("syscfg", cmdSysConfigCallback);

  gCmd = gCli.addCmd("t/erm", cmdCommandCallback);

  gCmd = gCli.addCmd("page", cmdPageCallback);
  gCmd.addPositionalArgument("index");
  gCmd.addPositionalArgument("page");

  gCmd = gCli.addCmd("zero", cmdZeroCallback);

  // Set error Callback
  gCli.setOnError(errorCallback);
}

////////////////////////////////////////////////////////////////////////////

static uint8_t gInputIndex = 0;
static char gInputBuffer[40] = "\0";

/// <summary>
/// return to ICM mode, resetting the input buffer and index, and printing a new prompt for the CLI
/// </summary>
static void returnToICM() {
  gInterface = 0x00;                      // return to ICM
  gInputIndex = 0;
  gInputBuffer[0] = '\0';

  Serial1.print("> ");                   // new prompt for CLI
}

/// <summary>
/// 
/// </summary>
/// <param name="c"></param>
static void normalInput(uint8_t c) {
  switch (c) {
  case ctrl('Z'):                         // ^Z
    returnToICM();
    break;

  default:
    if ((c == '\n') || (c == '\r')) {
      uint8_t lBuffer[COLS + 1];
      uint8_t* lPtr = lBuffer;

      writeVDUQ('\r');

      if (getAsScreenMode()) {
        vduGetCurrentScreenline(lBuffer);
//        Serial1.printf("*D: normalInput: [%s]\n\n", lBuffer);

        vduRestoreCursor();

        while (*lPtr) {
          if (! writeCPUQ(*lPtr++)){
            returnToICM();
            break;
          }
        }

        writeCPUQ('\r');                     // go
      }
    }
    else {
//      Serial1.printf("*D: ICM->VDU: [%02X]\n", c);
      writeVDUQ(c);
    }

    if (! getAsScreenMode()) {              // normal mode
      if (!writeCPUQ(c)) {
        returnToICM();
      }
    }
   
    break;
  }
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
          Serial1.print("> ");                   // new prompt
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
