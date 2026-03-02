// 
// 
// 
#include <arduino.h>
#include "mmu.h"
#include "cmd.h"
#include "neobus.h"

///-------------------------------------------------------------
/// CMD_LOT_BASE:
/// CMD_OUTCHAR: host -> write a char to this slot
/// CMD_INCHAR: 6502  -> read a char from this slot
/// CMD_COMMAND: host -> read a command code from this slot
/// 
#define CMD_SLOT_BASE    0xFFF0
#define CMD_SLOT_OUTCHAR 0        // write to 6502
#define CMD_SLOT_INCHAR  1        // read from 6502
#define CMD_SLOT_CMD     2
#define CMD_PARAM_BASE   (0x0100)

/// <summary>
/// init cmd slots: set to 0x00
/// </summary>
void initCmdSlots() {
  uint8_t ldata[3] = { 0x00, 0x00, 0x00 };

  snoop_write6502Memory(CMD_SLOT_BASE, 3, ldata);
  gMMUIOTrigger = false;
}

/// <summary>
/// readm a char from 6502 slot-interface
/// </summary>
/// <param name="vSlot"></param>
/// <param name="vData"></param>
inline __attribute__((always_inline))
uint8_t readCmdSlot(const uint8_t vSlot) {
  uint8_t lData;
  snoop_read6502Memory(CMD_SLOT_BASE + vSlot, 1, &lData);

  return lData;
}

/// <summary>
/// write a char to 6502 slot-interface
/// </summary>
/// <param name="vSlot"></param>
/// <param name="vData"></param>
inline __attribute__((always_inline))
void writeCmdSlot(const uint8_t vSlot, uint8_t vData) {
  uint8_t lData = vData;
  snoop_write6502Memory(CMD_SLOT_BASE + vSlot, 1, &lData);
}

// --------------------------------------------------------------

/// <summary>
/// read a char from 6502
/// </summary>
/// <param name="vChar"></param>
/// <returns></returns>
uint8_t inChar6502() {
  uint8_t lChar = 0x00;

  if (gMMUIOTrigger) {
    lChar = readCmdSlot(CMD_SLOT_INCHAR);
    if (lChar == 0x00) {
      return false;
    }
    else {
      writeCmdSlot(CMD_SLOT_INCHAR, 0x00);  // ACK
      gMMUIOTrigger = false;  // reset MMU IO trigger
    }
  }

  return lChar;
}

/// <summary>
/// OutcharAvailable: check if 6502 is ready to receive char
/// </summary>
/// <returns></returns>
bool outCharAvailable6502() {
  uint8_t lChar = readCmdSlot(CMD_SLOT_OUTCHAR);
  return (lChar == 0);
}

/// <summary>
/// write a char to 6502 non blocking, 
/// return false if last char is not ACK (0x00)
/// </summary>
/// <param name="vChar"></param>
bool outChar6502(const uint8_t vChar) {
  if (outCharAvailable6502()) {
    writeCmdSlot(CMD_SLOT_OUTCHAR, vChar);
    return true;
  }

  return false;
}

/// <summary>
/// write a char to 6502 blocking, 
/// wait for ACK (0x00) before write
/// </summary>
/// <param name="vChar"></param>
void  outCharBlocking6502(const uint8_t vChar) {
  while (! outCharAvailable6502()) {
    delay(5);                        // TODO ulgh
  }

  writeCmdSlot(CMD_SLOT_OUTCHAR, vChar);
}

/// <summary>
/// get command from 6502: read command code from 6502 slot-interface if MMU IO trigger is set, otherwise return 0x00,
/// </summary>
/// <returns></returns>
uint8_t getCommand6502() {
  uint8_t lCmd = 0x00;

  if (gMMUIOTrigger) {
    lCmd = readCmdSlot(CMD_SLOT_CMD);
  }

  return lCmd;
}

/// <summary>
/// ACK command: write 0x00 to CMD_SLOT_CMD to ACK command received,
/// </summary>
void ackCommand6502() {
  gMMUIOTrigger = false;  // reset MMU IO trigger

  writeCmdSlot(CMD_SLOT_CMD, 0x00);
}

/// <summary>
/// Read command params: read command params from 6502 memory,
/// </summary>
/// <returns></returns>
bool readCommandParams(uint8_t vNumParams, uint8_t vParamlist[]) {
  snoop_read6502Memory(CMD_PARAM_BASE, vNumParams, vParamlist);

  return true;
}
