/*
  basis of code taken from : http://forum.6502.org/viewtopic.php?t=3644
*/
#include <arduino.h>
#include "disasm6502.h"
#include "neobus.h"

// Padding for 1,2 & 3 byte instructions
static const char* padding[3] = { "       ","    "," " };

// 57 Instructions + Undefined ("???")
static const char* instruction[58] = {
  //       0     1     2     3     4     5     6     7     8     9
         "ADC","AND","ASL","BCC","BCS","BEQ","BIT","BMI","BNE","BPL", // 0
         "BRK","BVC","BVS","CLC","CLD","CLI","CLV","CMP","CPX","CPY", // 1
         "DEC","DEX","DEY","EOR","INC","INX","INY","JMP","JSR","LDA", // 2
         "LDX","LDY","LSR","NOP","ORA","PHA","PHP","PLA","PLP","ROL", // 3
         "ROR","ROT","RTI","RTS","SBC","SEC","SED","SEI","STA","STX", // 4
         "STY","TAX","TAY","TSX","TXA","TXS","TYA","???" };            // 5

// This is a lookup of the text formating required for mode output, plus one entry to distinguish relative mode
static const char* modes[9][2] = { {"",""},{"#",""},{"",",X"},{"",",Y"},{"(",",X)"},{"(","),Y"},{"(",")"},{"A",""},{"",""} };

// Opcode Properties for 256 opcodes {length_in_bytes, mnemonic_lookup, mode_chars_lookup}
static const uint8_t opcode_props[256][3] = {
  //         0        1        2        3        4        5        6        7        8        9        A        B        C        D        E        F
  //     ******** -------- ******** -------- ******** -------- ******** -------- ******** -------- ******** -------- ******** -------- ******** --------
         {1,10,0},{2,34,4},{1,57,0},{1,57,0},{1,57,0},{2,34,0},{2,2,0}, {1,57,0},{1,36,0},{2,34,1},{1,2,7}, {1,57,0},{1,57,0},{3,34,0},{3,2,0}, {1,57,0}, // 0
         {2,9,8}, {2,34,5},{1,57,0},{1,57,0},{1,57,0},{2,34,2},{2,2,2}, {1,57,0},{1,13,0},{3,34,3},{1,57,0},{1,57,0},{1,57,0},{3,34,2},{3,2,2}, {1,57,0}, // 1
         {3,28,0},{2,1,4}, {1,57,0},{1,57,0},{2,6,0}, {2,1,0}, {2,39,0},{1,57,0},{1,38,0},{2,1,1}, {1,39,7},{1,57,0},{3,6,0}, {3,1,0}, {3,39,0},{1,57,0}, // 2
         {2,7,8}, {2,1,5}, {1,57,0},{1,57,0},{1,57,0},{2,1,2}, {2,39,2},{1,57,0},{1,45,0},{3,1,3}, {1,57,0},{1,57,0},{1,57,0},{3,1,2}, {3,39,2},{1,57,0}, // 3
         {1,42,0},{2,23,4},{1,57,0},{1,57,0},{1,57,0},{2,23,0},{2,32,0},{1,57,0},{1,35,0},{2,23,1},{1,32,7},{1,57,0},{3,27,0},{3,23,0},{3,32,0},{1,57,0}, // 4
         {2,11,8},{2,23,5},{1,57,0},{1,57,0},{1,57,0},{2,23,2},{2,32,2},{1,57,0},{1,15,0},{3,23,3},{1,57,0},{1,57,0},{1,57,0},{3,23,2},{3,32,2},{1,57,0}, // 5
         {1,43,0},{2,0,4}, {1,57,0},{1,57,0},{1,57,0},{2,0,0}, {2,40,0},{1,57,0},{1,37,0},{2,0,1}, {1,40,7},{1,57,0},{3,27,6},{3,0,0}, {3,40,0},{1,57,0}, // 6
         {2,12,8},{2,0,5}, {1,57,0},{1,57,0},{1,57,0},{2,0,2}, {2,40,2},{1,57,0},{1,47,0},{3,0,3}, {1,57,0},{1,57,0},{1,57,0},{3,0,2}, {3,40,2},{1,57,0}, // 7
         {1,57,0},{2,48,4},{1,57,0},{1,57,0},{2,50,0},{2,48,0},{2,49,0},{1,57,0},{1,22,0},{1,57,0},{1,54,0},{1,57,0},{3,50,0},{3,48,0},{3,49,0},{1,57,0}, // 8
         {2,3,8}, {2,48,5},{1,57,0},{1,57,0},{2,50,2},{2,48,2},{2,49,3},{1,57,0},{1,56,0},{3,48,3},{1,55,0},{1,57,0},{1,57,0},{3,48,2},{1,57,0},{1,57,0}, // 9
         {2,31,1},{2,29,4},{2,30,1},{1,57,0},{2,31,0},{2,29,0},{2,30,0},{1,57,0},{1,52,0},{2,29,1},{1,51,0},{1,57,0},{3,31,0},{3,29,0},{3,30,0},{1,57,0}, // A
         {2,4,8}, {2,29,5},{1,57,0},{1,57,0},{2,31,2},{2,29,2},{2,30,3},{1,57,0},{1,16,0},{3,29,3},{1,53,0},{1,57,0},{3,31,2},{3,29,2},{3,30,3},{1,57,0}, // B
         {2,19,1},{2,17,4},{1,57,0},{1,57,0},{2,19,0},{2,17,0},{2,20,0},{1,57,0},{1,26,0},{2,17,1},{1,21,0},{1,57,0},{3,19,0},{3,17,0},{3,20,0},{1,57,0}, // C
         {2,8,8}, {2,17,5},{1,57,0},{1,57,0},{1,57,0},{2,17,2},{2,20,2},{1,57,0},{1,14,0},{3,17,3},{1,57,0},{1,57,0},{1,57,0},{3,17,2},{3,20,2},{1,57,0}, // D
         {2,18,1},{2,44,4},{1,57,0},{1,57,0},{2,18,0},{2,44,0},{2,24,0},{1,57,0},{1,25,0},{2,44,1},{1,33,0},{1,57,0},{3,18,0},{3,44,0},{3,24,0},{1,57,0}, // E
         {2,5,8}, {2,44,5},{1,57,0},{1,57,0},{1,57,0},{2,44,2},{2,24,2},{1,57,0},{1,46,0},{3,44,3},{1,57,0},{1,57,0},{1,57,0},{3,44,2},{3,24,2},{1,57,0}  // F
};

/// <summary>
/// 
/// </summary>
/// <param name="vAddress"></param>
/// <returns></returns>
uint16_t disasm6502(const uint16_t vAddress) {
  uint16_t address;
  uint8_t buffer[4];
  int currentbyte = 0;
  int previousbyte = 0;
  uint8_t paramcount = 0;
  uint8_t addrmode;
  const char* opcode;
  const char* pad;
  const char* pre;
  const char* post;

  address = vAddress;

  snoop_read6502Memory(vAddress, 3, buffer);

  for (uint8_t i = 0; i < 3; ++i) {                                 //Start proccessing loop.
    previousbyte = currentbyte;
    currentbyte = buffer[i];
    if (paramcount == 0) {
      Serial.printf("%04X   ", address);                            //Display current address at beginning of line
      paramcount = opcode_props[currentbyte][0];              //Get instruction length
      opcode = instruction[opcode_props[currentbyte][1]];     //Get opcode name
      addrmode = opcode_props[currentbyte][2];                //Get info required to display addressing mode
      pre = modes[addrmode][0];                               //Look up pre-operand formatting text
      post = modes[addrmode][1];                              //Look up post-operand formatting text
      pad = padding[(paramcount - 1)];                        //Calculate correct padding for output alignment
      address = address + paramcount;                         //Increment address
    }

    if (paramcount != 0)                                        //Keep track of possition within instruction
      paramcount--;

    Serial.printf("%02X ", currentbyte);                              //Display the current byte in HEX

    if (paramcount == 0) {
      Serial.printf(" %s %s %s", pad, opcode, pre);                  //Pad text, display instruction name and pre-operand chars
      if (!strcmp(pad, "    ")) {                             //Check if single operand instruction
        if (addrmode != 8) {                                //If not using relative addressing ...
          Serial.printf("$%02X", currentbyte);                   //...display operand
        }
        else {                                            //Addressing mode is relative...
          Serial.printf("$%04X", (address + ((currentbyte < 128) ? currentbyte : currentbyte - 256))); //...display relative address.
        }
      }

      if (!strcmp(pad, " "))                                   //Check if two operand instruction and if so...
        Serial.printf("$%02X%02X", currentbyte, previousbyte);     //...display operand
      Serial.printf("%s\n", post);                                   //Display post-operand chars

      return address;
    }
  }
  //    printf("$%04X                .END\n", address);                 //Add .END directive to end of output
  return address;
}
