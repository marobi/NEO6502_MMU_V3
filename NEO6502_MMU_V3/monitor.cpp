//
// simple RPI monitor:
// see help
// 
#include <arduino.h>
#include <SimpleCLI.h>

#include "config.h"
#include "bios.h"
#include "monitor.h"
#include "p6502.h"
#include "mmu.h"
#include "ram.h"
#include "neobus.h"
#include "disasm6502.h"


// Create CLI Object
static SimpleCLI gCli;
// Commands
static Command gCmd;

/// <summary>
/// converts text string hex > integer
/// </summary>
/// <param name="s"></param>
/// <returns></returns>
inline __attribute__((always_inline))
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
void cmdResetCallback(cmd* c) {
  Command cmd(c); // Create wrapper object

  // Get argument
  String lBus = cmd.getArgument("bus").getValue();
  switch (lBus[0]) {
  case 'd':
    set6502State(eRESET, eDISABLED);
    Serial.println("Reset disabled");
    break;

  case 'e':
    set6502State(eRESET, eENABLED);
    Serial.println("Reset enabled");
    break;

  default:
    Serial.println("*E: Reset: invalid parameter");
    break;
  }
}

/// <summary>
/// CLOCK on or off
/// </summary>
/// <param name="c"></param>
void cmdClockCallback(cmd* c) {
  Command cmd(c); // Create wrapper object

  // Get argument
  String lBus = cmd.getArgument("state").getValue();
  switch (lBus[1]) {
  case 'n':
    // clock on
    set6502Clock(DEFAULT_6502_CLOCK);
    Serial.println("Clock ON");
    break;
  case 'f':
    // clock off
    reset6502Clock();
    Serial.println("Clock OFF");
    break;
  default:
    Serial.println("*E: Clock: invalid parameter");
    break;
  }
}

/// <summary>
/// GO after halt or reset
/// </summary>
/// <param name="c"></param>
void cmdGoCallback(cmd* c) {
  Command cmd(c); // Create wrapper object

  set6502State(eRUN, eENABLED);
  Serial.println("Go");
}

/// <summary>
/// SINGLE STEP CPU
/// </summary>
/// <param name="c"></param>
void cmdSSCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("steps").getValue();
  uint8_t lStep = atoi(arg1.c_str()) & 0xFF;

  singleStep6502(lStep, true);
}

/// <summary>
/// STOP
/// </summary>
/// <param name="c"></param>
void cmdStopCallback(cmd* c) {
  Command cmd(c); // Create wrapper object

  // Get argument
  String lBus = cmd.getArgument("bus").getValue();
  switch (lBus[0]) {
  case 'd':
    set6502State(eHALTED, eDISABLED);
    Serial.println("Stop disabled");
    break;

  case 'e':
    set6502State(eHALTED, eENABLED);
    Serial.println("Stop enabled");
    break;

  default:
    Serial.println("*E: Stop: invalid parameter");
    break;
  }
}

/// <summary>
/// DUMP memory
/// </summary>
/// <param name="c"></param>
void cmdDumpCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("from").getValue();
  String arg2 = cmd.getArgument("to").getValue();
  uint16_t lFrom = x2i(arg1.c_str()) & 0XFFFF;
  uint16_t lTo = max(x2i(arg2.c_str()) & 0XFFFF, lFrom + 15);
  if (lTo <= lFrom) lTo = 0xFFFF;
  Serial.printf("Dump %04X - %04X\n", lFrom, lTo);

  dumpMemory(lFrom, lTo);
}

/// <summary>
/// DISASM memory
/// </summary>
/// <param name="c"></param>
void cmdDisAsmCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("from").getValue();
  String arg2 = cmd.getArgument("to").getValue();
  uint16_t lFrom = x2i(arg1.c_str()) & 0XFFFF;
  uint16_t lTo = max(x2i(arg2.c_str()) & 0XFFFF, lFrom + 15);
  if (lTo <= lFrom) lTo = 0xFFFF;
  Serial.printf("Disassembly %04X - %04X\n", lFrom, lTo);

  uint16_t lAddress = lFrom;
  while (lAddress < lTo) {
    lAddress = disasm6502(lAddress);
  }

  Serial.println();
}

/// <summary>
/// MEMORY alter
/// </summary>
/// <param name="c"></param>
void cmdMemCallback(cmd* c) {
  uint16_t lStartAddress, lAddress;
  uint8_t lData;
  Command cmd(c);

  int lCountArgs = cmd.countArgs(); // Get number of arguments
  if (lCountArgs < 2) {
    Serial.println("*E: not enough parameters\n");
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
    Serial.printf("Dump %04X - %04X\n", lStartAddress, --lAddress);
    dumpMemory(lStartAddress, lAddress);
  }
}

/// <summary>
/// STATUS
/// </summary>
/// <param name="c"></param>
void cmdStatusCallback(cmd* c) {
  Serial.printf("Status\n");
  state6502();
  Serial.printf("*I: MMU: %02X\n", readMMUContext());
}

/// <summary>
/// IRQ
/// </summary>
/// <param name="c"></param>
void cmdIRQCallback(cmd* c) {
  Serial.printf("IRQ\n");
}

/// <summary>
/// MMU context change
/// </summary>
/// <param name="c"></param>
void cmdMMUCallback(cmd* c) {
  Command cmd(c);
  String arg1 = cmd.getArgument("context").getValue();

  uint8_t lContext = x2i(arg1.c_str()) & 0x7F;  // 128

  Serial.printf("MMU: %02X\n", lContext);

  writeMMUContext(lContext);  // set the context
  dumpMMUContext(lContext);   // dump context
}

/// <summary>
/// MMU context index page change
/// </summary>
/// <param name="c"></param>
void cmdPageCallback(cmd* c) {
  Command cmd(c);

  String arg1 = cmd.getArgument("index").getValue();
  String arg2 = cmd.getArgument("page").getValue();
  uint8_t lContext = readMMUContext();
  uint8_t lIndex = x2i(arg1.c_str()) & 0xFF;
  uint8_t lPage = x2i(arg2.c_str()) & 0xFF;
  uint8_t lPrevPage = readMMUPage(lContext, lIndex);
  writeMMUPage(lContext, lIndex, lPage);

  Serial.printf("MMU page: %02X:%02X %02X => %02X\n", lContext, lIndex, lPrevPage, lPage);
}

/// <summary>
/// help overview of commands
/// </summary>
/// <param name="c"></param>
void cmdHelpCallback(cmd* c) {
  Serial.print("RPI ICM help:\n\
 cl/ock <on|off>       turn clock on|off\n\
 r/eset <d|e>          reset <disabled | enabled>\n\
 s/top <d|e>           stop <disabled | enabled> \n\
 g/o                   go\n\
 ss <steps>            single step (steps)\n\
 i/rq                  generate IRQ\n\
 d/ump <from> <to>     dump memory\n\
 dis <from> <to>       disasm memory\n\
 m/em <address> <data> modify memory address(es)\n\
 st/at                 status of cpus/bus\n\
 mmu  <context>        set mmu context\n\
 page <index> <page>   set mmu page\n\
 help                  help\n\
\n");
}

/// <summary>
/// CLI error handler
/// </summary>
/// <param name="e"></param>
void errorCallback(cmd_error* e) {
  CommandError cmdError(e); // Create wrapper object

  Serial.print("*E: ");
  Serial.println(cmdError.toString());

  if (cmdError.hasCommand()) {
    Serial.print("*I: Did you mean \"");
    Serial.print(cmdError.getCommand().toString());
    Serial.println("\"?");
  }
}

/// <summary>
/// init the monitor commands
/// </summary>
void initMonitor() {
  Serial.printf("RPI I.C.M. (%s) %s\n> ", BIOS_CPU, MON_VERSION);

  // Create the commands with callback function
  gCmd = gCli.addCmd("cl/ock", cmdClockCallback);
  gCmd.addPositionalArgument("state", "off");

  gCmd = gCli.addCmd("r/eset", cmdResetCallback);
  gCmd.addPositionalArgument("bus", "d");

  gCmd = gCli.addCmd("g/o", cmdGoCallback);

  gCmd = gCli.addCmd("ss", cmdSSCallback);
  gCmd.addPositionalArgument("steps", "1");

  gCmd = gCli.addCmd("s/top", cmdStopCallback);
  gCmd.addPositionalArgument("bus", "d");

  gCmd = gCli.addCmd("d/ump", cmdDumpCallback);
  gCmd.addPositionalArgument("from");
  gCmd.addPositionalArgument("to", "0");

  gCmd = gCli.addCmd("dis", cmdDisAsmCallback);
  gCmd.addPositionalArgument("from");
  gCmd.addPositionalArgument("to", "0");

  gCmd = gCli.addBoundlessCommand("m/em", cmdMemCallback);

  gCmd = gCli.addCmd("st/at", cmdStatusCallback);

  gCmd = gCli.addCmd("i/rq", cmdIRQCallback);

  gCmd = gCli.addCmd("mmu", cmdMMUCallback);
  gCmd.addPositionalArgument("context", "0");

  gCmd = gCli.addCmd("page", cmdPageCallback);
  gCmd.addPositionalArgument("index");
  gCmd.addPositionalArgument("page");

  gCmd = gCli.addCmd("h/elp", cmdHelpCallback);

  // Set error Callback
  gCli.setOnError(errorCallback);
}

////////////////////////////////////////////////////////////////////////////

/// <summary>
/// rpi monitor to control the HW
/// </summary>
/// <returns>void</returns>
void monitor() {
  static uint8_t gInputIndex = 0;
  static char gInputBuffer[40] = "\0";
  int c;

  // Check if user typed something on keyboard
  if (Serial.available()) {
    c = Serial.read();
    switch (c) {
    case -1:                                   // no char
      break;
    case '\b':                                 // backspace
    case 127:                                  // delete
      if (gInputIndex >= 1) {
        gInputIndex--;                         // delete last char
        gInputBuffer[gInputIndex] = '\0';      // from buffer
        Serial.print(" \b");
      }
      else
        Serial.print(" ");                     // buffer is empty
      break;
    case '\n':                                 // CR
    case '\r':                                 // LF
      // Parse the user input into the CLI
      gCli.parse(gInputBuffer);

      Serial.print("> ");                     // new prompt
      gInputIndex = 0;                        // new buffer
      gInputBuffer[0] = '\0';
      break;

    default:                                  // enter in buffer
      gInputBuffer[gInputIndex++] = c;
      gInputBuffer[gInputIndex] = '\0';
      break;
    }
  }
}
