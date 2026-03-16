/*
   W65C02 Disassembler
   - integrated opcode table
   - two-pass label discovery
   - automatic vector labels (NMI, RESET, IRQ)
*/

#include <Arduino.h>
#include "disasm6502.h"
#include "neobus.h"

#define LABEL_WIDTH 7

/* --------------------------------------------------
   Addressing modes
-------------------------------------------------- */

enum AddrMode
{
  IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABSX, ABSY, IND, INDX, INDY, ZPIND, REL
};

struct Opcode
{
  const char* name;
  uint8_t mode;
  uint8_t len;
};

/* --------------------------------------------------
   Label support
-------------------------------------------------- */

#define MAX_LABELS 128

struct Label
{
  uint16_t addr;
  char name[8];
  bool fixed;
};

static Label labels[MAX_LABELS];
static uint8_t labelCount = 0;

static const char* findLabel(uint16_t addr)
{
  for (uint8_t i = 0; i < labelCount; i++)
    if (labels[i].addr == addr)
      return labels[i].name;

  return nullptr;
}

static void createNamedLabel(uint16_t addr, const char* name)
{
  if (labelCount >= MAX_LABELS)
    return;

  Label* l = &labels[labelCount++];

  l->addr = addr;
  strncpy(l->name, name, sizeof(l->name));
  l->name[sizeof(l->name) - 1] = 0;
  l->fixed = true;
}

static const char* createLabel(uint16_t addr)
{
  const char* existing = findLabel(addr);
  if (existing)
    return existing;

  if (labelCount >= MAX_LABELS)
    return nullptr;

  Label* l = &labels[labelCount++];

  l->addr = addr;
  sprintf(l->name, "L%04X", addr);
  l->fixed = false;

  return l->name;
}

void disasmResetLabels()
{
  labelCount = 0;
}

/* --------------------------------------------------
   Memory helper
-------------------------------------------------- */

static uint16_t readWord(uint16_t addr)
{
  uint8_t b[2];
  snoop_read6502Memory(addr, 2, b);
  return b[0] | (b[1] << 8);
}

/* --------------------------------------------------
   Seed interrupt vectors
-------------------------------------------------- */

static void seedVectors()
{
  uint16_t nmi = readWord(0xFFFA);
  uint16_t reset = readWord(0xFFFC);
  uint16_t irq = readWord(0xFFFE);

  createNamedLabel(nmi, "NMI");
  createNamedLabel(reset, "RESET");
  createNamedLabel(irq, "IRQ");
}

/* --------------------------------------------------
   W65C02 opcode table
-------------------------------------------------- */

static const Opcode optable[256] PROGMEM =
{
{"BRK",IMP,1},{"ORA",INDX,2},{"???",IMP,1},{"???",IMP,1},{"TSB",ZP,2},{"ORA",ZP,2},{"ASL",ZP,2},{"RMB0",ZP,2},
{"PHP",IMP,1},{"ORA",IMM,2},{"ASL",ACC,1},{"???",IMP,1},{"TSB",ABS,3},{"ORA",ABS,3},{"ASL",ABS,3},{"BBR0",REL,3},

{"BPL",REL,2},{"ORA",INDY,2},{"ORA",ZPIND,2},{"???",IMP,1},{"TRB",ZP,2},{"ORA",ZPX,2},{"ASL",ZPX,2},{"RMB1",ZP,2},
{"CLC",IMP,1},{"ORA",ABSY,3},{"INC",ACC,1},{"???",IMP,1},{"TRB",ABS,3},{"ORA",ABSX,3},{"ASL",ABSX,3},{"BBR1",REL,3},

{"JSR",ABS,3},{"AND",INDX,2},{"???",IMP,1},{"???",IMP,1},{"BIT",ZP,2},{"AND",ZP,2},{"ROL",ZP,2},{"RMB2",ZP,2},
{"PLP",IMP,1},{"AND",IMM,2},{"ROL",ACC,1},{"???",IMP,1},{"BIT",ABS,3},{"AND",ABS,3},{"ROL",ABS,3},{"BBR2",REL,3},

{"BMI",REL,2},{"AND",INDY,2},{"AND",ZPIND,2},{"???",IMP,1},{"BIT",ZPX,2},{"AND",ZPX,2},{"ROL",ZPX,2},{"RMB3",ZP,2},
{"SEC",IMP,1},{"AND",ABSY,3},{"DEC",ACC,1},{"???",IMP,1},{"BIT",ABSX,3},{"AND",ABSX,3},{"ROL",ABSX,3},{"BBR3",REL,3},

{"RTI",IMP,1},{"EOR",INDX,2},{"???",IMP,1},{"???",IMP,1},{"???",IMP,1},{"EOR",ZP,2},{"LSR",ZP,2},{"RMB4",ZP,2},
{"PHA",IMP,1},{"EOR",IMM,2},{"LSR",ACC,1},{"???",IMP,1},{"JMP",ABS,3},{"EOR",ABS,3},{"LSR",ABS,3},{"BBR4",REL,3},

{"BVC",REL,2},{"EOR",INDY,2},{"EOR",ZPIND,2},{"???",IMP,1},{"???",IMP,1},{"EOR",ZPX,2},{"LSR",ZPX,2},{"RMB5",ZP,2},
{"CLI",IMP,1},{"EOR",ABSY,3},{"PHY",IMP,1},{"???",IMP,1},{"???",IMP,1},{"EOR",ABSX,3},{"LSR",ABSX,3},{"BBR5",REL,3},

{"RTS",IMP,1},{"ADC",INDX,2},{"???",IMP,1},{"???",IMP,1},{"STZ",ZP,2},{"ADC",ZP,2},{"ROR",ZP,2},{"RMB6",ZP,2},
{"PLA",IMP,1},{"ADC",IMM,2},{"ROR",ACC,1},{"???",IMP,1},{"JMP",IND,3},{"ADC",ABS,3},{"ROR",ABS,3},{"BBR6",REL,3},

{"BVS",REL,2},{"ADC",INDY,2},{"ADC",ZPIND,2},{"???",IMP,1},{"STZ",ZPX,2},{"ADC",ZPX,2},{"ROR",ZPX,2},{"RMB7",ZP,2},
{"SEI",IMP,1},{"ADC",ABSY,3},{"PLY",IMP,1},{"???",IMP,1},{"JMP",ABSX,3},{"ADC",ABSX,3},{"ROR",ABSX,3},{"BBR7",REL,3},

{"BRA",REL,2},{"STA",INDX,2},{"???",IMP,1},{"???",IMP,1},{"STY",ZP,2},{"STA",ZP,2},{"STX",ZP,2},{"SMB0",ZP,2},
{"DEY",IMP,1},{"BIT",IMM,2},{"TXA",IMP,1},{"???",IMP,1},{"STY",ABS,3},{"STA",ABS,3},{"STX",ABS,3},{"BBS0",REL,3},

{"BCC",REL,2},{"STA",INDY,2},{"STA",ZPIND,2},{"???",IMP,1},{"STY",ZPX,2},{"STA",ZPX,2},{"STX",ZPY,2},{"SMB1",ZP,2},
{"TYA",IMP,1},{"STA",ABSY,3},{"TXS",IMP,1},{"???",IMP,1},{"STZ",ABS,3},{"STA",ABSX,3},{"STZ",ABSX,3},{"BBS1",REL,3},

{"LDY",IMM,2},{"LDA",INDX,2},{"LDX",IMM,2},{"???",IMP,1},{"LDY",ZP,2},{"LDA",ZP,2},{"LDX",ZP,2},{"SMB2",ZP,2},
{"TAY",IMP,1},{"LDA",IMM,2},{"TAX",IMP,1},{"???",IMP,1},{"LDY",ABS,3},{"LDA",ABS,3},{"LDX",ABS,3},{"BBS2",REL,3},

{"BCS",REL,2},{"LDA",INDY,2},{"LDA",ZPIND,2},{"???",IMP,1},{"LDY",ZPX,2},{"LDA",ZPX,2},{"LDX",ZPY,2},{"SMB3",ZP,2},
{"CLV",IMP,1},{"LDA",ABSY,3},{"TSX",IMP,1},{"???",IMP,1},{"LDY",ABSX,3},{"LDA",ABSX,3},{"LDX",ABSY,3},{"BBS3",REL,3},

{"CPY",IMM,2},{"CMP",INDX,2},{"???",IMP,1},{"???",IMP,1},{"CPY",ZP,2},{"CMP",ZP,2},{"DEC",ZP,2},{"SMB4",ZP,2},
{"INY",IMP,1},{"CMP",IMM,2},{"DEX",IMP,1},{"???",IMP,1},{"CPY",ABS,3},{"CMP",ABS,3},{"DEC",ABS,3},{"BBS4",REL,3},

{"BNE",REL,2},{"CMP",INDY,2},{"CMP",ZPIND,2},{"???",IMP,1},{"???",IMP,1},{"CMP",ZPX,2},{"DEC",ZPX,2},{"SMB5",ZP,2},
{"CLD",IMP,1},{"CMP",ABSY,3},{"PHX",IMP,1},{"???",IMP,1},{"???",IMP,1},{"CMP",ABSX,3},{"DEC",ABSX,3},{"BBS5",REL,3},

{"CPX",IMM,2},{"SBC",INDX,2},{"???",IMP,1},{"???",IMP,1},{"CPX",ZP,2},{"SBC",ZP,2},{"INC",ZP,2},{"SMB6",ZP,2},
{"INX",IMP,1},{"SBC",IMM,2},{"NOP",IMP,1},{"???",IMP,1},{"CPX",ABS,3},{"SBC",ABS,3},{"INC",ABS,3},{"BBS6",REL,3},

{"BEQ",REL,2},{"SBC",INDY,2},{"SBC",ZPIND,2},{"???",IMP,1},{"???",IMP,1},{"SBC",ZPX,2},{"INC",ZPX,2},{"SMB7",ZP,2},
{"SED",IMP,1},{"SBC",ABSY,3},{"PLX",IMP,1},{"???",IMP,1},{"???",IMP,1},{"SBC",ABSX,3},{"INC",ABSX,3},{"BBS7",REL,3}
};

/* --------------------------------------------------
   Label scan
-------------------------------------------------- */

static void scanLabels(uint16_t addr, uint16_t lines)
{
  for (uint16_t i = 0; i < lines; i++)
  {
    uint8_t b[3];
    snoop_read6502Memory(addr, 3, b);

    Opcode op;
    memcpy_P(&op, &optable[b[0]], sizeof(Opcode));

    if (op.mode == REL)
      createLabel(addr + 2 + (int8_t)b[1]);

    if (op.mode == ABS)
      if (!strcmp(op.name, "JSR") || !strcmp(op.name, "JMP"))
        createLabel((b[2] << 8) | b[1]);

    addr += op.len;
  }
}

/* --------------------------------------------------
   Disassembler
-------------------------------------------------- */

uint16_t disasm6502(uint16_t addr, const uint16_t lines, const bool simple)
{
  if (!simple) {
    disasmResetLabels();
    seedVectors();
    scanLabels(addr, lines);
  }

  for (uint16_t line = 0; line < lines; line++) {
    const char* lbl = findLabel(addr);

    if (lbl) {
      Serial1.println();
      char labelbuf[10];
      snprintf(labelbuf, sizeof(labelbuf), "%s:", lbl);
      Serial1.printf("%-*s", LABEL_WIDTH, labelbuf);
    }
    else
      Serial1.printf("       ");

    uint8_t b[3];
    snoop_read6502Memory(addr, 3, b);

    Opcode op;
    memcpy_P(&op, &optable[b[0]], sizeof(Opcode));

    Serial1.printf("%04X  ", addr);

    Serial1.printf("%02X ", b[0]);
    if (op.len > 1) Serial1.printf("%02X ", b[1]); else Serial1.printf("   ");
    if (op.len > 2) Serial1.printf("%02X ", b[2]); else Serial1.printf("   ");

    Serial1.printf(" %-4s ", op.name);

    switch (op.mode)
    {
    case IMM:  Serial1.printf("#$%02X", b[1]); break;
    case ZP:   Serial1.printf("$%02X", b[1]); break;
    case ZPX:  Serial1.printf("$%02X,X", b[1]); break;
    case ZPY:  Serial1.printf("$%02X,Y", b[1]); break;

    case ABS:
    {
      uint16_t t = (b[2] << 8) | b[1];
      const char* l = findLabel(t);
      if (l) Serial1.printf("%s", l);
      else Serial1.printf("$%04X", t);
      break;
    }

    case ABSX: Serial1.printf("$%02X%02X,X", b[2], b[1]); break;
    case ABSY: Serial1.printf("$%02X%02X,Y", b[2], b[1]); break;
    case IND:  Serial1.printf("($%02X%02X)", b[2], b[1]); break;
    case INDX: Serial1.printf("($%02X,X)", b[1]); break;
    case INDY: Serial1.printf("($%02X),Y", b[1]); break;
    case ZPIND:Serial1.printf("($%02X)", b[1]); break;

    case REL:{
      uint16_t t = addr + 2 + (int8_t)b[1];
      const char* l = findLabel(t);
      if (l) Serial1.printf("%s", l);
      else Serial1.printf("$%04X", t);
      break;
    }

    case ACC: Serial1.printf("A"); break;
    default: break;
    }

    Serial1.println();
    addr += op.len;
  }

  return addr;
}
