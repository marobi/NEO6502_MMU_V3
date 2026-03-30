// 
// 
// 
#include <arduino.h>
#include "cmd_proc.h"

/// <summary>
/// 
/// </summary>
typedef struct {
  uint8_t Groupid;
  char    Cmdname[16];
  uint8_t Argcount;
  uint8_t Firstarg;
  bool   (*Callback)();
} tCPDef;

/// <summary>
/// dummy callback function
/// </summary>
/// <returns></returns>
bool cbDummy() {
  return true;
};


/// <summary>
/// Command Processor definitions
/// </summary>
tCPDef CPcommands[] = {
  {  CP_GROUP_GDU, "CLS",  0,  0, cbDummy },

  { -1,            "UNK",  0,  0, cbDummy}
};

/// <summary>
/// 
/// </summary>
/// <param name="vGroup"></param>
/// <param name="vCmd"></param>
/// <returns></returns>
bool execCPCmd(const uint8_t vGroup, const uint8_t vCmd) {
  return true;
}

/// <summary>
/// 
/// </summary>
void dumpCProcessor() {

};

/// <summary>
/// 
/// </summary>
void initCProcessor() {

};
