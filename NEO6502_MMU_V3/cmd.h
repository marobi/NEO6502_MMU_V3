#pragma once

void initCmdSlots();

uint8_t inChar6502();

bool outChar6502(const uint8_t vChar);

bool outCharAvailable6502();

void outCharBlocking6502(const uint8_t vChar);

uint8_t getCommand6502();

void ackCommand6502();

bool readCommandParams(uint8_t vNumParams, uint8_t vParamlist[]);
