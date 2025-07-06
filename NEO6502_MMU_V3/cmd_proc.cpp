// 
// 
// 
#include <arduino.h>
#include "cmd_proc.h"

typedef struct {
  char     Name[16];
  bool   (*Callback)();
} tCmdCmd;

typedef  struct {
  uint8_t  Id;
  char     Name[16];
  uint8_t  CmdCount;
  tCmdCmd* Commands;
} tCmdGroup;

//
uint8_t   gCmdGroupIdx = 0;
tCmdGroup gCmdGroup[MAX_CMD_GROUPS];

/// <summary>
/// dummy cb for function
/// </summary>
/// <returns></returns>
bool cbDummy() {
  return true;
};

/// <summary>
/// define a command group
/// </summary>
/// <param name="vName"></param>
/// <param name="vCmdCount"></param>
bool setCmdGroup(const uint8_t vId, const char* vName, const uint8_t vCmdCount) {
  for (uint8_t i = 0; i < gCmdGroupIdx; i++) {
    if (gCmdGroup[i].Id == vId) {  // update
      strcpy(gCmdGroup[i].Name, vName);
      return true;
    }
  }
  if (gCmdGroupIdx < MAX_CMD_GROUPS) {
    tCmdGroup *t = &gCmdGroup[gCmdGroupIdx];

    t->Id = vId;                             // new
    strcpy(t->Name, vName);
    t->CmdCount = vCmdCount;

    t->Commands = (tCmdCmd*)malloc(sizeof(tCmdCmd) * vCmdCount);
    for (uint8_t j = 0; j < vCmdCount; j++) {
      strcpy(t->Commands[j].Name, "Dummy");
      t->Commands[j].Callback = cbDummy;
    }

    gCmdGroupIdx++;
    return true;
  }
  else
    return false;
};

/// <summary>
/// define a command in a command group
/// </summary>
/// <param name="vGrp"></param>
/// <param name="vId"></param>
/// <param name="Name"></param>
/// <param name="vFunc"></param>
bool setCmdCmd(const uint8_t vGrp, const uint8_t vId, const char* vName, bool(*vFunc)()) {
  for (uint8_t g = 0; g < gCmdGroupIdx; g++) {
    if (gCmdGroup[g].Id == vGrp) {
      tCmdGroup *t = &gCmdGroup[g];
      strcpy(t->Commands[vId].Name, vName);
      t->Commands[vId].Callback = vFunc;

      return true;
    }
  }

  return false;
};

/// <summary>
/// 
/// </summary>
/// <param name="vGrp"></param>
/// <param name="vCmd"></param>
/// <returns></returns>
bool execCommand(const uint8_t vGrp, const uint8_t vCmd) {
  for (uint8_t g = 0; g < gCmdGroupIdx; g++) {
    if (gCmdGroup[g].Id == vGrp) {
      if (vCmd < gCmdGroup[g].CmdCount) {

        return gCmdGroup[g].Commands[vCmd].Callback();
      }
      else
        return false;
    }
  }

  return false;
}

/// <summary>
/// 
/// </summary>
void dumpCmdProcessor() {
  Serial.println("Cmd proc:");

  for (uint8_t i = 0; i < gCmdGroupIdx; i++) {
    tCmdGroup* t = &gCmdGroup[i];
    Serial.printf("%d\t%s\t#%d\n", t->Id, t->Name, t->CmdCount);

    tCmdCmd* cmd = t->Commands;

    for (uint8_t c = 0; c < t->CmdCount; c++) {
      Serial.printf("\t%d\t%s\n", c, cmd[c].Name);
    }
  }
  Serial.println();
};

/// <summary>
/// 
/// </summary>
void initCmdProcessor() {

  setCmdGroup(0, "BIOS", 2);
  setCmdCmd(0, 0, "INCHAR", cbDummy);
  setCmdCmd(0, 1, "OUTCHAR", cbDummy);

  setCmdGroup(1, "VDU", 4);
  setCmdCmd(1, 0, "CLS", cbDummy);
  setCmdCmd(1, 1, "CURSOR", cbDummy);
  setCmdCmd(1, 2, "COLOR", cbDummy);
  setCmdCmd(1, 3, "BGCOLOR", cbDummy);

  setCmdGroup(127, "DEBUG", 2);
  setCmdCmd(127, 0, "SET", cbDummy);
  setCmdCmd(127, 1, "RESET", cbDummy);

//  dumpCmdProcessor();
  Serial.println("*I: command proc OK");
};
