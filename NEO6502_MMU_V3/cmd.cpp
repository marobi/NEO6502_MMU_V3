// 
// 
// 
#include <arduino.h>
#include "cmd.h"
#include "neobus.h"

#define CMD_SLOT_BASE   (0xFC00)

#define CMD_INCHAR      (0u)
#define CMD_OUTCHAR     (1u)

/// <summary>
/// 
/// </summary>
/// <param name="vSlot"></param>
/// <param name="vData"></param>
inline __attribute__((always_inline))
void readCmdSlot(const uint8_t vSlot, uint8_t* vData) {
  snoop_read6502Memory(CMD_SLOT_BASE + vSlot, 1, vData);
}

/// <summary>
/// 
/// </summary>
/// <param name="vSlot"></param>
/// <param name="vData"></param>
inline __attribute__((always_inline))
void writeCmdSlot(const uint8_t vSlot, uint8_t vData) {
  snoop_write6502Memory(CMD_SLOT_BASE + vSlot, 1, &vData);
}

/// <summary>
/// read a char from 6502 interface
/// </summary>
/// <param name="vChar"></param>
/// <returns></returns>
bool read6502Char(uint8_t *vChar) {
  uint8_t lChar;
  
  readCmdSlot(CMD_OUTCHAR, &lChar);
  if (lChar == 0x00) {
    return false;
  }
  else {
    *vChar = lChar;
    writeCmdSlot(CMD_OUTCHAR, 0x00);  // ACK
    return true;
  }
}

/// <summary>
/// write a char to 6502 interface
/// </summary>
/// <param name="vChar"></param>
bool write6502Char(const uint8_t vChar) {
  uint8_t lChar;

  readCmdSlot(CMD_INCHAR, &lChar);
  if (lChar != 0x00) {
    return false;
  }
  else {  // last is ACK
    writeCmdSlot(CMD_INCHAR, vChar);
    return true;
  }
}

