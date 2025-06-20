// 
// simple RPI monitor:
// commands:
// r <d>           reset <disabled>
// g               go from reset
// h <d>           halt <disabled>
// i               generate IRQ
// d <from> <to>   dump memory
// m <mem> <dat>   modify memory loc
// s               status of cpus/bus
// L               load tbi
// S               save tbi
// h               help
// x               exit monitor
// 

#include "arduino.h"
#include "monitor.h"

//#define MUM_MON_CMDS   (11)

//
typedef struct tCmd {
  char    cmd;
  uint8_t args;
  bool optional;
} dCmd;

//
dCmd Mon[] = {
  {'r', 1, true},
  {'g', 0, true},
  {'h', 0, true},
  {'i', 0, true},
  {'d', 2, false},
  {'m', 2, false},
  {'s', 0, true},
  {'L', 1, true},
  {'S', 1, true},
  {'h', 0, true},
  {'x', 0, true},
  {NULL, 0, true}
};

// parsed command
static char cmdCmd;

// index in command line
static uint8_t cmdIndex;

// parsed arguments
static char cmdArgs[2][8];

/// <summary>
/// 
/// </summary>
/// <param name="Cmd"></param>
/// <returns></returns>
bool parseCmd(const String Cmd) {
  char lCmd;
  uint8_t c = 0;

  while (Mon[c].cmd != NULL) {
    if (Mon[c].cmd == Cmd[0]) {
      // got it
      cmdCmd = Mon[c].cmd;

      return true;
    }
    else
      c++;
  }

  return false;
}

/// <summary>
/// 
/// </summary>
void monitor() {
  String lLine;

  while (true) {
    // prompt
    Serial.printf("> ");
    Serial.flush();
    // read a command line
    while (!Serial.available());

    lLine = Serial.readStringUntil('\n');
    if (parseCmd(lLine)) {
      Serial.printf(" OK\n");
      switch (cmdCmd) {
      case 'x':
        return;
        break;
      };
    }
    else
      Serial.printf(" *no such command\n");
  }
}

/// <summary>
/// 
/// </summary>
void exitMonitor() {
  Serial.printf("Goodbye\n");
}

/// <summary>
/// 
/// </summary>
void enterMonitor() {
  Serial.printf("RPI-6502 mon %s\n", MON_VERSION);

  monitor();

  exitMonitor();
}

