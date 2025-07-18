// https://www.masswerk.at/6502/6502_instruction_set.html
// TODO: IO
// TODO: decimal mode
// TODO: interrupts?
// TODO: cycles?
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_U8() m->memory[m->pc++]
#define READ_U16() (READ_U8() | (READ_U8() << 8))

#define FLAG_N 0x80
#define FLAG_V 0x40
#define FLAG_D 0x08
#define FLAG_I 0x04
#define FLAG_Z 0x02
#define FLAG_C 0x01

typedef struct MOS6502 {
  uint8_t *memory;
  uint16_t pc;
  uint8_t A;
  uint8_t X;
  uint8_t Y;
  uint8_t SP;
  uint8_t P;

  uint8_t display_modified_this_cycle;
} MOS6502;

void mos6502_disassemble(MOS6502 *m);

MOS6502 mos6502_create(void) {
  MOS6502 m = {0};
  m.pc = 0x600;
  m.memory = calloc(1, 1 << 16);
  m.SP = 0xFF;
  m.P = 0x24;
  return m;
}

void mos6502_set_flag(MOS6502 *m, uint8_t flag, uint8_t value) {
  if (value) {
    m->P |= flag;
  } else {
    m->P &= ~flag;
  }
}

void mos6502_set_zn(MOS6502 *m, uint8_t v) {
  mos6502_set_flag(m, FLAG_Z, v == 0);
  mos6502_set_flag(m, FLAG_N, v & 0x80);
}

uint8_t mos6502_mem_read(MOS6502 *m, uint16_t addr) { return m->memory[addr]; }

void mos6502_mem_write(MOS6502 *m, uint16_t addr, uint8_t v) {
  m->memory[addr] = v;
  if (0x0400 <= addr && addr < 0x0600) {
    m->display_modified_this_cycle = 1;
  }
}

void mos6502_step(MOS6502 *m) {
  m->display_modified_this_cycle = 0;

  uint8_t op = READ_U8();
  switch (op) {
  case 0x00: {
    // TODO: its supposed to do some interrupt magic
    exit(0);
  } break;
  case 0x01: {
    uint16_t oper = READ_U8() + m->X;
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    m->A |= mos6502_mem_read(m, addr);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x05: {
    m->A |= mos6502_mem_read(m, READ_U8());
    mos6502_set_zn(m, m->A);
  } break;
  case 0x06: {
    uint8_t addr = READ_U8();
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = oper << 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, (oper >> 7) & 1);
    mos6502_set_zn(m, res);
  } break;
  case 0x08: {
    mos6502_mem_write(m, 0x0100 + m->SP--, m->P | 0x30);
  } break;
  case 0x09: {
    m->A |= READ_U8();
    mos6502_set_zn(m, m->A);
  } break;
  case 0x0A: {
    uint8_t carry = (m->A & 0x80) != 0;
    m->A <<= 1;
    mos6502_set_flag(m, FLAG_C, carry);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x0D: {
    m->A |= mos6502_mem_read(m, READ_U16());
    mos6502_set_zn(m, m->A);
  } break;
  case 0x0E: {
    uint16_t addr = READ_U16();
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = oper << 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, (oper & 0x80) != 0);
    mos6502_set_zn(m, res);
  } break;
  case 0x10: {
    int8_t offset = READ_U8();
    if (!(m->P & FLAG_N)) {
      m->pc += offset;
    }
  } break;
  case 0x11: {
    uint16_t oper = READ_U8();
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    m->A |= mos6502_mem_read(m, addr + m->Y);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x15: {
    m->A |= mos6502_mem_read(m, READ_U8() + m->X);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x16: {
    uint8_t addr = READ_U8() + m->X;
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = oper << 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, (oper >> 7) & 1);
    mos6502_set_zn(m, res);
  } break;
  case 0x18: {
    mos6502_set_flag(m, FLAG_C, 0);
  } break;
  case 0x19: {
    m->A |= mos6502_mem_read(m, READ_U16() + m->Y);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x1D: {
    m->A |= mos6502_mem_read(m, READ_U16() + m->X);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x1E: {
    uint16_t addr = READ_U16() + m->X;
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = oper << 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, (oper & 0x80) != 0);
    mos6502_set_zn(m, res);
  } break;
  case 0x20: {
    uint16_t target = READ_U16();
    uint16_t ret_addr = m->pc - 1;
    mos6502_mem_write(m, 0x0100 + m->SP--, (uint8_t)(ret_addr >> 8));
    mos6502_mem_write(m, 0x0100 + m->SP--, (uint8_t)ret_addr);
    m->pc = target;
  } break;
  case 0x21: {
    uint16_t oper = READ_U8() + m->X;
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    m->A &= mos6502_mem_read(m, addr);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x24: {
    uint8_t oper = mos6502_mem_read(m, READ_U8());
    mos6502_set_flag(m, FLAG_Z, (m->A & oper) == 0);
    mos6502_set_flag(m, FLAG_N, oper & 0x80);
    mos6502_set_flag(m, FLAG_V, oper & 0x40);
  } break;
  case 0x25: {
    m->A &= mos6502_mem_read(m, READ_U8());
    mos6502_set_zn(m, m->A);
  } break;
  case 0x26: {
    uint16_t addr = READ_U8();
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = (oper << 1) | ((m->P & FLAG_C) ? 1 : 0);
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, (oper & 0x80) >> 7);
    mos6502_set_zn(m, res);
  } break;
  case 0x28: {
    m->P = mos6502_mem_read(m, 0x0100 + (++m->SP));
  } break;
  case 0x29: {
    m->A &= READ_U8();
    mos6502_set_zn(m, m->A);
  } break;
  case 0x2A: {
    uint8_t old_carry = m->P & FLAG_C;
    uint8_t new_carry = (m->A & 0x80) >> 7;
    m->A = ((m->A << 1) & 0xFE) | old_carry;
    mos6502_set_flag(m, FLAG_C, new_carry);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x2C: {
    uint8_t oper = mos6502_mem_read(m, READ_U16());
    mos6502_set_flag(m, FLAG_Z, (m->A & oper) == 0);
    mos6502_set_flag(m, FLAG_N, oper & 0x80);
    mos6502_set_flag(m, FLAG_V, oper & 0x40);
  } break;
  case 0x2D: {
    m->A &= mos6502_mem_read(m, READ_U16());
    mos6502_set_zn(m, m->A);
  } break;
  case 0x2E: {
    uint16_t addr = READ_U16();
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = (oper << 1) | ((m->P & FLAG_C) ? 1 : 0);
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, (oper & 0x80) >> 7);
    mos6502_set_zn(m, res);
  } break;
  case 0x30: {
    int8_t offset = READ_U8();
    if (m->P & FLAG_N) {
      m->pc += offset;
    }
  } break;
  case 0x31: {
    uint16_t oper = READ_U8();
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    m->A &= mos6502_mem_read(m, addr + m->Y);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x35: {
    m->A &= mos6502_mem_read(m, READ_U8() + m->X);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x36: {
    uint16_t addr = READ_U8() + m->X;
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = (oper << 1) | ((m->P & FLAG_C) ? 1 : 0);
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, (oper & 0x80) >> 7);
    mos6502_set_zn(m, res);
  } break;
  case 0x38: {
    mos6502_set_flag(m, FLAG_C, 1);
  } break;
  case 0x39: {
    m->A &= mos6502_mem_read(m, READ_U16() + m->Y);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x3D: {
    m->A &= mos6502_mem_read(m, READ_U16() + m->X);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x3E: {
    uint16_t addr = READ_U16() + m->X;
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = (oper << 1) | ((m->P & FLAG_C) ? 1 : 0);
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, (oper & 0x80) >> 7);
    mos6502_set_zn(m, res);
  } break;
  case 0x40: {
    m->P = mos6502_mem_read(m, 0x0100 + (++m->SP));
    uint16_t low = mos6502_mem_read(m, 0x0100 + (++m->SP));
    uint16_t high = mos6502_mem_read(m, 0x0100 + (++m->SP));
    m->pc = (((uint16_t)high << 8) | low);
  } break;
  case 0x41: {
    uint16_t oper = READ_U8() + m->X;
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    m->A ^= mos6502_mem_read(m, addr);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x45: {
    m->A ^= mos6502_mem_read(m, READ_U8());
    mos6502_set_zn(m, m->A);
  } break;
  case 0x46: {
    uint8_t addr = READ_U8();
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = oper >> 1;
    mos6502_set_flag(m, FLAG_C, oper & 0x01);
    mos6502_set_zn(m, res);
    mos6502_mem_write(m, addr, res);
  } break;
  case 0x48: {
    mos6502_mem_write(m, 0x0100 + m->SP--, m->A);
  } break;
  case 0x49: {
    m->A ^= READ_U8();
    mos6502_set_zn(m, m->A);
  } break;
  case 0x4A: {
    mos6502_set_flag(m, FLAG_C, m->A & 0x01);
    m->A >>= 1;
    mos6502_set_zn(m, m->A);
  } break;
  case 0x4C: {
    m->pc = READ_U16();
  } break;
  case 0x4D: {
    m->A ^= mos6502_mem_read(m, READ_U16());
    mos6502_set_zn(m, m->A);
  } break;
  case 0x4E: {
    uint16_t addr = READ_U16();
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = oper >> 1;
    mos6502_set_flag(m, FLAG_C, oper & 0x01);
    mos6502_set_zn(m, res);
    mos6502_mem_write(m, addr, res);
  } break;
  case 0x50: {
    int8_t offset = READ_U8();
    if (!(m->P & FLAG_V)) {
      m->pc += offset;
    }
  } break;
  case 0x51: {
    uint16_t oper = READ_U8();
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    m->A ^= mos6502_mem_read(m, addr + m->Y);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x55: {
    m->A ^= mos6502_mem_read(m, READ_U8() + m->X);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x56: {
    uint8_t addr = READ_U8() + m->X;
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = oper >> 1;
    mos6502_set_flag(m, FLAG_C, oper & 0x01);
    mos6502_set_zn(m, res);
    mos6502_mem_write(m, addr, res);
  } break;
  case 0x58: {
    mos6502_set_flag(m, FLAG_I, 0);
  } break;
  case 0x59: {
    m->A ^= mos6502_mem_read(m, READ_U16() + m->Y);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x5D: {
    m->A ^= mos6502_mem_read(m, READ_U16() + m->X);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x5E: {
    uint16_t addr = READ_U16() + m->X;
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = oper >> 1;
    mos6502_set_flag(m, FLAG_C, oper & 0x01);
    mos6502_set_zn(m, res);
    mos6502_mem_write(m, addr, res);
  } break;
  case 0x60: {
    uint16_t low = mos6502_mem_read(m, 0x0100 + (++m->SP));
    uint16_t high = mos6502_mem_read(m, 0x0100 + (++m->SP));
    m->pc = (((uint16_t)high << 8) | low) + 1;
  } break;
  case 0x61: {
    uint8_t oper = READ_U8() + m->X;
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    uint8_t value = mos6502_mem_read(m, addr);
    uint16_t res = (uint16_t)m->A + value + (m->P & FLAG_C);

    mos6502_set_flag(m, FLAG_C, res > 0xFF);
    mos6502_set_flag(m, FLAG_V, (~(m->A ^ value) & (m->A ^ res) & 0x80));
    m->A = res;
    mos6502_set_zn(m, m->A);
  } break;
  case 0x65: {
    uint8_t oper = mos6502_mem_read(m, READ_U8());
    uint16_t res = m->A + oper + (m->P & FLAG_C);
    mos6502_set_flag(m, FLAG_C, res > 0xFF);
    mos6502_set_flag(m, FLAG_V, (~(m->A ^ oper) & (m->A ^ res) & 0x80) != 0);
    mos6502_set_zn(m, res);
    m->A = res;
  } break;
  case 0x66: {
    uint16_t addr = READ_U8();
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = (oper >> 1) | ((m->P & FLAG_C) << 7);
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, oper & 0x01);
    mos6502_set_zn(m, res);
  } break;
  case 0x68: {
    m->A = mos6502_mem_read(m, 0x0100 + (++m->SP));
    mos6502_set_zn(m, m->A);
  } break;
  case 0x69: {
    uint8_t oper = READ_U8();
    uint16_t res = m->A + oper + (m->P & FLAG_C);
    mos6502_set_flag(m, FLAG_C, res > 0xFF);
    mos6502_set_flag(m, FLAG_V, (~(m->A ^ oper) & (m->A ^ res) & 0x80) != 0);
    mos6502_set_zn(m, res);
    m->A = res;
  } break;
  case 0x6A: {
    uint8_t carry = m->A & 0x01;
    m->A = (m->A >> 1) | ((m->P & FLAG_C) << 7);
    mos6502_set_flag(m, FLAG_C, carry);
    mos6502_set_zn(m, m->A);
  } break;
  case 0x6C: {
    uint16_t oper = READ_U16();
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    m->pc = addr;
  } break;
  case 0x6D: {
    uint8_t oper = mos6502_mem_read(m, READ_U16());
    uint16_t res = m->A + oper + (m->P & FLAG_C);
    mos6502_set_flag(m, FLAG_C, res > 0xFF);
    mos6502_set_flag(m, FLAG_V, (~(m->A ^ oper) & (m->A ^ res) & 0x80) != 0);
    mos6502_set_zn(m, res);
    m->A = res;
  } break;
  case 0x6E: {
    uint16_t addr = READ_U16();
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = (oper >> 1) | ((m->P & FLAG_C) << 7);
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, oper & 0x01);
    mos6502_set_zn(m, res);
  } break;
  case 0x70: {
    int8_t offset = READ_U8();
    if (m->P & FLAG_V) {
      m->pc += offset;
    }
  } break;
  case 0x71: {
    uint8_t oper = READ_U8();
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    uint8_t value = mos6502_mem_read(m, addr + m->Y);
    uint16_t res = (uint16_t)m->A + value + (m->P & FLAG_C);

    mos6502_set_flag(m, FLAG_C, res > 0xFF);
    mos6502_set_flag(m, FLAG_V, (~(m->A ^ value) & (m->A ^ res) & 0x80));
    m->A = res;
    mos6502_set_zn(m, m->A);
  } break;
  case 0x75: {
    uint8_t oper = mos6502_mem_read(m, (uint8_t)(READ_U8() + m->X));
    uint16_t res = m->A + oper + (m->P & FLAG_C);
    mos6502_set_flag(m, FLAG_C, res > 0xFF);
    mos6502_set_flag(m, FLAG_V, (~(m->A ^ oper) & (m->A ^ res) & 0x80) != 0);
    mos6502_set_zn(m, res);
    m->A = res;
  } break;
  case 0x76: {
    uint8_t addr = READ_U8() + m->X;
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = (oper >> 1) | ((m->P & FLAG_C) << 7);
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, oper & 0x01);
    mos6502_set_zn(m, res);
  } break;
  case 0x78: {
    mos6502_set_flag(m, FLAG_I, 1);
  } break;
  case 0x79: {
    uint8_t oper = mos6502_mem_read(m, READ_U16() + m->Y);
    uint16_t res = m->A + oper + (m->P & FLAG_C);
    mos6502_set_flag(m, FLAG_C, res > 0xFF);
    mos6502_set_flag(m, FLAG_V, (~(m->A ^ oper) & (m->A ^ res) & 0x80) != 0);
    mos6502_set_zn(m, res);
    m->A = res;
  } break;
  case 0x7D: {
    uint8_t oper = mos6502_mem_read(m, READ_U16() + m->X);
    uint16_t res = m->A + oper + (m->P & FLAG_C);
    mos6502_set_flag(m, FLAG_C, res > 0xFF);
    mos6502_set_flag(m, FLAG_V, (~(m->A ^ oper) & (m->A ^ res) & 0x80) != 0);
    mos6502_set_zn(m, res);
    m->A = res;
  } break;
  case 0x7E: {
    uint16_t addr = READ_U16() + m->X;
    uint8_t oper = mos6502_mem_read(m, addr);
    uint8_t res = (oper >> 1) | ((m->P & FLAG_C) << 7);
    mos6502_mem_write(m, addr, res);
    mos6502_set_flag(m, FLAG_C, oper & 0x01);
    mos6502_set_zn(m, res);
  } break;
  case 0x81: {
    uint8_t oper = READ_U8() + m->X;
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    mos6502_mem_write(m, addr, m->A);
  } break;
  case 0x84: {
    mos6502_mem_write(m, READ_U8(), m->Y);
  } break;
  case 0x85: {
    mos6502_mem_write(m, READ_U8(), m->A);
  } break;
  case 0x86: {
    mos6502_mem_write(m, READ_U8(), m->X);
  } break;
  case 0x88: {
    m->Y--;
    mos6502_set_zn(m, m->Y);
  } break;
  case 0x8A: {
    m->A = m->X;
    mos6502_set_zn(m, m->A);
  } break;
  case 0x8C: {
    mos6502_mem_write(m, READ_U16(), m->Y);
  } break;
  case 0x8D: {
    mos6502_mem_write(m, READ_U16(), m->A);
  } break;
  case 0x8E: {
    mos6502_mem_write(m, READ_U16(), m->X);
  } break;
  case 0x90: {
    int8_t offset = READ_U8();
    if (!(m->P & FLAG_C)) {
      m->pc += offset;
    }
  } break;
  case 0x91: {
    uint8_t oper = READ_U8();
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    mos6502_mem_write(m, addr + m->Y, m->A);
  } break;
  case 0x94: {
    mos6502_mem_write(m, READ_U8() + m->X, m->Y);
  } break;
  case 0x95: {
    mos6502_mem_write(m, READ_U8() + m->X, m->A);
  } break;
  case 0x96: {
    mos6502_mem_write(m, READ_U8() + m->Y, m->X);
  } break;
  case 0x98: {
    m->A = m->Y;
    mos6502_set_zn(m, m->A);
  } break;
  case 0x99: {
    mos6502_mem_write(m, READ_U16() + m->Y, m->A);
  } break;
  case 0x9A: {
    m->SP = m->X;
  } break;
  case 0x9D: {
    mos6502_mem_write(m, READ_U16() + m->X, m->A);
  } break;
  case 0xA0: {
    m->Y = READ_U8();
    mos6502_set_zn(m, m->Y);
  } break;
  case 0xA1: {
    uint8_t oper = READ_U8() + m->X;
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    m->A = mos6502_mem_read(m, addr);
    mos6502_set_zn(m, m->A);
  } break;
  case 0xA2: {
    m->X = READ_U8();
    mos6502_set_zn(m, m->X);
  } break;
  case 0xA4: {
    m->Y = mos6502_mem_read(m, READ_U8());
    mos6502_set_zn(m, m->Y);
  } break;
  case 0xA5: {
    m->A = mos6502_mem_read(m, READ_U8());
    mos6502_set_zn(m, m->A);
  } break;
  case 0xA6: {
    m->X = mos6502_mem_read(m, READ_U8());
    mos6502_set_zn(m, m->X);
  } break;
  case 0xA8: {
    m->Y = m->A;
    mos6502_set_zn(m, m->Y);
  } break;
  case 0xA9: {
    m->A = READ_U8();
    mos6502_set_zn(m, m->A);
  } break;
  case 0xAA: {
    m->X = m->A;
    mos6502_set_zn(m, m->X);
  } break;
  case 0xAC: {
    m->Y = mos6502_mem_read(m, READ_U16());
    mos6502_set_zn(m, m->Y);
  } break;
  case 0xAD: {
    m->A = mos6502_mem_read(m, READ_U16());
    mos6502_set_zn(m, m->A);
  } break;
  case 0xAE: {
    m->X = mos6502_mem_read(m, READ_U16());
    mos6502_set_zn(m, m->X);
  } break;
  case 0xB0: {
    int8_t offset = READ_U8();
    if (m->P & FLAG_C) {
      m->pc += offset;
    }
  } break;
  case 0xB1: {
    uint8_t oper = READ_U8();
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    m->A = mos6502_mem_read(m, addr + m->Y);
    mos6502_set_zn(m, m->A);
  } break;
  case 0xB4: {
    m->Y = mos6502_mem_read(m, READ_U8() + m->X);
    mos6502_set_zn(m, m->Y);
  } break;
  case 0xB5: {
    m->A = mos6502_mem_read(m, READ_U8() + m->X);
    mos6502_set_zn(m, m->A);
  } break;
  case 0xB6: {
    m->X = mos6502_mem_read(m, READ_U8() + m->Y);
    mos6502_set_zn(m, m->X);
  } break;
  case 0xB8: {
    mos6502_set_flag(m, FLAG_V, 0);
  } break;
  case 0xB9: {
    m->A = mos6502_mem_read(m, READ_U16() + m->Y);
    mos6502_set_zn(m, m->A);
  } break;
  case 0xBA: {
    m->Y = m->SP;
    mos6502_set_zn(m, m->Y);
  } break;
  case 0xBC: {
    m->Y = mos6502_mem_read(m, READ_U16() + m->X);
    mos6502_set_zn(m, m->Y);
  } break;
  case 0xBD: {
    m->A = mos6502_mem_read(m, READ_U16() + m->X);
    mos6502_set_zn(m, m->A);
  } break;
  case 0xBE: {
    m->X = mos6502_mem_read(m, READ_U16() + m->Y);
    mos6502_set_zn(m, m->X);
  } break;
  case 0xC0: {
    uint8_t oper = READ_U8();
    uint16_t res = m->Y - oper;
    mos6502_set_flag(m, FLAG_C, m->Y >= oper);
    mos6502_set_zn(m, res);
  } break;
  case 0xC1: {
    uint8_t oper = READ_U8() + m->X;
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    uint8_t res = mos6502_mem_read(m, addr);
    mos6502_set_flag(m, FLAG_C, m->A >= res);
    mos6502_set_zn(m, m->A - res);
  } break;
  case 0xC4: {
    uint8_t oper = mos6502_mem_read(m, READ_U8());
    uint16_t res = m->Y - oper;
    mos6502_set_flag(m, FLAG_C, m->Y >= oper);
    mos6502_set_zn(m, res);
  } break;
  case 0xC5: {
    uint8_t oper = mos6502_mem_read(m, READ_U8());
    mos6502_set_flag(m, FLAG_C, m->A >= oper);
    mos6502_set_zn(m, m->A - oper);
  } break;
  case 0xC6: {
    uint16_t addr = READ_U8();
    uint8_t res = mos6502_mem_read(m, addr) - 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_zn(m, res);
  } break;
  case 0xC8: {
    m->Y++;
    mos6502_set_zn(m, m->Y);
  } break;
  case 0xC9: {
    uint8_t oper = READ_U8();
    mos6502_set_flag(m, FLAG_C, m->A >= oper);
    mos6502_set_zn(m, m->A - oper);
  } break;
  case 0xCA: {
    m->X--;
    mos6502_set_zn(m, m->X);
  } break;
  case 0xCC: {
    uint8_t oper = mos6502_mem_read(m, READ_U16());
    uint16_t res = m->Y - oper;
    mos6502_set_flag(m, FLAG_C, m->Y >= oper);
    mos6502_set_zn(m, res);
  } break;
  case 0xCD: {
    uint8_t oper = mos6502_mem_read(m, READ_U16());
    mos6502_set_flag(m, FLAG_C, m->A >= oper);
    mos6502_set_zn(m, m->A - oper);
  } break;
  case 0xCE: {
    uint16_t addr = READ_U16();
    uint8_t res = mos6502_mem_read(m, addr) - 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_zn(m, res);
  } break;
  case 0xD0: {
    int8_t offset = READ_U8();
    if (!(m->P & FLAG_Z)) {
      m->pc += offset;
    }
  } break;
  case 0xD1: {
    uint8_t oper = READ_U8();
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    uint8_t res = mos6502_mem_read(m, addr + m->Y);
    mos6502_set_flag(m, FLAG_C, m->A >= res);
    mos6502_set_zn(m, m->A - res);
  } break;
  case 0xD5: {
    uint8_t oper = mos6502_mem_read(m, READ_U8() + m->X);
    mos6502_set_flag(m, FLAG_C, m->A >= oper);
    mos6502_set_zn(m, m->A - oper);
  } break;
  case 0xD6: {
    uint16_t addr = READ_U8() + m->X;
    uint8_t res = mos6502_mem_read(m, addr) - 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_zn(m, res);
  } break;
  case 0xD8: {
    mos6502_set_flag(m, FLAG_D, 0);
  } break;
  case 0xD9: {
    uint8_t oper = mos6502_mem_read(m, READ_U16() + m->Y);
    mos6502_set_flag(m, FLAG_C, m->A >= oper);
    mos6502_set_zn(m, m->A - oper);
  } break;
  case 0xDD: {
    uint8_t oper = mos6502_mem_read(m, READ_U16() + m->X);
    mos6502_set_flag(m, FLAG_C, m->A >= oper);
    mos6502_set_zn(m, m->A - oper);
  } break;
  case 0xDE: {
    uint16_t addr = READ_U16() + m->X;
    uint8_t res = mos6502_mem_read(m, addr) - 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_zn(m, res);
  } break;
  case 0xE0: {
    uint8_t oper = READ_U8();
    uint16_t res = m->X - oper;
    mos6502_set_flag(m, FLAG_C, m->X >= oper);
    mos6502_set_zn(m, res);
  } break;
  case 0xE1: {
    uint8_t oper = READ_U8() + m->X;
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    uint8_t value = mos6502_mem_read(m, addr);
    uint16_t res = m->A - value - ((m->P & FLAG_C) ? 0 : 1);
    mos6502_set_flag(m, FLAG_C, res < 0x100);
    mos6502_set_flag(m, FLAG_V, (m->A ^ value) & (m->A ^ res) & 0x80);
    m->A = res;
    mos6502_set_zn(m, m->A);
  } break;
  case 0xE4: {
    uint8_t oper = mos6502_mem_read(m, READ_U8());
    uint16_t res = m->X - oper;
    mos6502_set_flag(m, FLAG_C, m->X >= oper);
    mos6502_set_zn(m, res);
  } break;
  case 0xE5: {
    uint8_t oper = mos6502_mem_read(m, READ_U8());
    uint16_t res = m->A - oper - ((m->P & FLAG_C) ? 0 : 1);
    mos6502_set_flag(m, FLAG_C, res < 0x100);
    mos6502_set_flag(m, FLAG_V, (m->A ^ oper) & (m->A ^ res) & 0x80);
    m->A = res;
    mos6502_set_zn(m, m->A);
  } break;
  case 0xE6: {
    uint16_t addr = READ_U8();
    uint8_t res = mos6502_mem_read(m, addr) + 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_zn(m, res);
  } break;
  case 0xE8: {
    m->X++;
    mos6502_set_zn(m, m->X);
  } break;
  case 0xE9: {
    uint8_t oper = READ_U8();
    uint16_t res = m->A - oper - ((m->P & FLAG_C) ? 0 : 1);
    mos6502_set_flag(m, FLAG_C, res < 0x100);
    mos6502_set_flag(m, FLAG_V, (m->A ^ oper) & (m->A ^ res) & 0x80);
    m->A = res;
    mos6502_set_zn(m, m->A);
  } break;
  case 0xEA: {
  } break;
  case 0xEC: {
    uint8_t oper = mos6502_mem_read(m, READ_U16());
    uint16_t res = m->X - oper;
    mos6502_set_flag(m, FLAG_C, m->X >= oper);
    mos6502_set_zn(m, res);
  } break;
  case 0xED: {
    uint8_t oper = mos6502_mem_read(m, READ_U16());
    uint16_t res = m->A - oper - ((m->P & FLAG_C) ? 0 : 1);
    mos6502_set_flag(m, FLAG_C, res < 0x100);
    mos6502_set_flag(m, FLAG_V, (m->A ^ oper) & (m->A ^ res) & 0x80);
    m->A = res;
    mos6502_set_zn(m, m->A);
  } break;
  case 0xEE: {
    uint16_t addr = READ_U16();
    uint8_t res = mos6502_mem_read(m, addr) + 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_zn(m, res);
  } break;
  case 0xF0: {
    int8_t offset = READ_U8();
    if (m->P & FLAG_Z) {
      m->pc += offset;
    }
  } break;
  case 0xF1: {
    uint8_t oper = READ_U8();
    uint16_t addr = mos6502_mem_read(m, oper) |
                    (mos6502_mem_read(m, (uint8_t)(oper + 1)) << 8);
    uint8_t value = mos6502_mem_read(m, addr + m->Y);
    uint16_t res = m->A - value - ((m->P & FLAG_C) ? 0 : 1);
    mos6502_set_flag(m, FLAG_C, res < 0x100);
    mos6502_set_flag(m, FLAG_V, (m->A ^ value) & (m->A ^ res) & 0x80);
    m->A = res;
    mos6502_set_zn(m, m->A);
  } break;
  case 0xF5: {
    uint8_t oper = mos6502_mem_read(m, READ_U8() + m->X);
    uint16_t res = m->A - oper - ((m->P & FLAG_C) ? 0 : 1);
    mos6502_set_flag(m, FLAG_C, res < 0x100);
    mos6502_set_flag(m, FLAG_V, (m->A ^ oper) & (m->A ^ res) & 0x80);
    m->A = res;
    mos6502_set_zn(m, m->A);
  } break;
  case 0xF6: {
    uint16_t addr = READ_U8() + m->X;
    uint8_t res = mos6502_mem_read(m, addr) + 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_zn(m, res);
  } break;
  case 0xF8: {
    mos6502_set_flag(m, FLAG_D, 1);
  } break;
  case 0xF9: {
    uint8_t oper = mos6502_mem_read(m, READ_U16() + m->Y);
    uint16_t res = m->A - oper - ((m->P & FLAG_C) ? 0 : 1);
    mos6502_set_flag(m, FLAG_C, res < 0x100);
    mos6502_set_flag(m, FLAG_V, (m->A ^ oper) & (m->A ^ res) & 0x80);
    m->A = res;
    mos6502_set_zn(m, m->A);
  } break;
  case 0xFD: {
    uint8_t oper = mos6502_mem_read(m, READ_U16() + m->X);
    uint16_t res = m->A - oper - ((m->P & FLAG_C) ? 0 : 1);
    mos6502_set_flag(m, FLAG_C, res < 0x100);
    mos6502_set_flag(m, FLAG_V, (m->A ^ oper) & (m->A ^ res) & 0x80);
    m->A = res;
    mos6502_set_zn(m, m->A);
  } break;
  case 0xFE: {
    uint16_t addr = READ_U16() + m->X;
    uint8_t res = mos6502_mem_read(m, addr) + 1;
    mos6502_mem_write(m, addr, res);
    mos6502_set_zn(m, res);
  } break;
  default:
    fprintf(stderr, "unrecognized opcode: $%02X\n", op);
    exit(1);
  }
}

void mos6502_disassemble(MOS6502 *m) {
  uint8_t op = READ_U8();
  switch (op) {
  case 0x00: {
    printf("BRK\t");
  } break;
  case 0x01: {
    printf("ORA ($%02X,X)", READ_U8());
  } break;
  case 0x05: {
    printf("ORA $%02X", READ_U8());
  } break;
  case 0x06: {
    printf("ASL $%02X", READ_U8());
  } break;
  case 0x08: {
    printf("PHP\t");
  } break;
  case 0x09: {
    printf("ORA #$%02X\t", READ_U8());
  } break;
  case 0x0A: {
    printf("ASL A");
  } break;
  case 0x0D: {
    printf("ORA $%04X", READ_U16());
  } break;
  case 0x0E: {
    printf("ASL $%04X", READ_U16());
  } break;
  case 0x10: {
    int8_t offset = READ_U8();
    printf("BPL $%04X", m->pc + offset);
  } break;
  case 0x11: {
    printf("ORA ($%02X),Y", READ_U8());
  } break;
  case 0x15: {
    printf("ORA $%02X,X", READ_U8());
  } break;
  case 0x16: {
    printf("ASL $%02X,X", READ_U8());
  } break;
  case 0x18: {
    printf("CLC\t");
  } break;
  case 0x19: {
    printf("ORA $%04X,Y", READ_U16());
  } break;
  case 0x1D: {
    printf("ORA $%04X,X", READ_U16());
  } break;
  case 0x1E: {
    printf("ASL $%04X,X", READ_U16());
  } break;
  case 0x20: {
    printf("JSR $%04X", READ_U16());
  } break;
  case 0x21: {
    printf("AND ($%02X,X)", READ_U8());
  } break;
  case 0x24: {
    printf("BIT $%02X", READ_U8());
  } break;
  case 0x25: {
    printf("AND $%02X", READ_U8());
  } break;
  case 0x26: {
    printf("ROL $%02X", READ_U8());
  } break;
  case 0x28: {
    printf("PLP\t");
  } break;
  case 0x29: {
    printf("AND #$%02X\t", READ_U8());
  } break;
  case 0x2A: {
    printf("ROL A");
  } break;
  case 0x2C: {
    printf("BIT $%04X", READ_U16());
  } break;
  case 0x2D: {
    printf("AND $%04X", READ_U16());
  } break;
  case 0x2E: {
    printf("ROL $%04X", READ_U16());
  } break;
  case 0x30: {
    int8_t offset = READ_U8();
    printf("BMI $%04X", m->pc + offset);
  } break;
  case 0x31: {
    printf("AND ($%02X),Y", READ_U8());
  } break;
  case 0x35: {
    printf("AND $%02X,X", READ_U8());
  } break;
  case 0x36: {
    printf("ROL $%02X,X", READ_U8());
  } break;
  case 0x38: {
    printf("SEC\t");
  } break;
  case 0x39: {
    printf("AND $%04X,Y", READ_U16());
  } break;
  case 0x3D: {
    printf("AND $%04X,X", READ_U16());
  } break;
  case 0x3E: {
    printf("ROL $%04X,X", READ_U16());
  } break;
  case 0x40: {
    printf("RTI\t");
  } break;
  case 0x41: {
    printf("EOR ($%02X,X)", READ_U8());
  } break;
  case 0x45: {
    printf("EOR $%02X", READ_U8());
  } break;
  case 0x46: {
    printf("LSR $%02X", READ_U8());
  } break;
  case 0x48: {
    printf("PHA\t");
  } break;
  case 0x49: {
    printf("EOR #$%02X\t", READ_U8());
  } break;
  case 0x4A: {
    printf("LSR A\t");
  } break;
  case 0x4C: {
    printf("JMP $%04X", READ_U16());
  } break;
  case 0x4D: {
    printf("EOR $%04X", READ_U16());
  } break;
  case 0x4E: {
    printf("LSR $%04X", READ_U16());
  } break;
  case 0x50: {
    int8_t offset = READ_U8();
    printf("BVC $%04X", m->pc + offset);
  } break;
  case 0x51: {
    printf("EOR ($%02X),Y", READ_U8());
  } break;
  case 0x55: {
    printf("EOR $%02X,X", READ_U8());
  } break;
  case 0x56: {
    printf("LSR $%02X,X", READ_U8());
  } break;
  case 0x58: {
    printf("CLI\t");
  } break;
  case 0x59: {
    printf("EOR $%04X,Y", READ_U16());
  } break;
  case 0x5D: {
    printf("EOR $%04X,X", READ_U16());
  } break;
  case 0x5E: {
    printf("LSR $%04X,X", READ_U16());
  } break;
  case 0x60: {
    printf("RTS\t");
  } break;
  case 0x61: {
    printf("ADC ($%02X,X)", READ_U8());
  } break;
  case 0x65: {
    printf("ADC $%02X", READ_U8());
  } break;
  case 0x66: {
    printf("ROR $%02X", READ_U8());
  } break;
  case 0x68: {
    printf("PLA\t");
  } break;
  case 0x69: {
    printf("ADC #$%02X\t", READ_U8());
  } break;
  case 0x6A: {
    printf("ROR A");
  } break;
  case 0x6C: {
    printf("JMP ($%04X)", READ_U16());
  } break;
  case 0x6D: {
    printf("ADC $%04X", READ_U16());
  } break;
  case 0x6E: {
    printf("ROR $%04X", READ_U16());
  } break;
  case 0x70: {
    int8_t offset = READ_U8();
    printf("BVS $%04X", m->pc + offset);
  } break;
  case 0x71: {
    printf("ADC ($%02X),Y", READ_U8());
  } break;
  case 0x75: {
    printf("ADC $%02X,X", READ_U8());
  } break;
  case 0x76: {
    printf("ROR $%02X,X", READ_U8());
  } break;
  case 0x78: {
    printf("SEI\t");
  } break;
  case 0x79: {
    printf("ADC $%04X,Y", READ_U16());
  } break;
  case 0x7D: {
    printf("ADC $%04X,X", READ_U16());
  } break;
  case 0x7E: {
    printf("ROR $%04X,X", READ_U16());
  } break;
  case 0x81: {
    printf("STA ($%02X,X)", READ_U8());
  } break;
  case 0x84: {
    printf("STY $%02X\t", READ_U8());
  } break;
  case 0x85: {
    printf("STA $%02X\t", READ_U8());
  } break;
  case 0x86: {
    printf("STX $%02X\t", READ_U8());
  } break;
  case 0x88: {
    printf("DEY\t");
  } break;
  case 0x8A: {
    printf("TXA\t");
  } break;
  case 0x8C: {
    printf("STY $%04X", READ_U16());
  } break;
  case 0x8D: {
    printf("STA $%04X", READ_U16());
  } break;
  case 0x8E: {
    printf("STX $%04X", READ_U16());
  } break;
  case 0x90: {
    int8_t offset = READ_U8();
    printf("BCC $%04X", m->pc + offset);
  } break;
  case 0x91: {
    printf("STA ($%02X),Y", READ_U8());
  } break;
  case 0x94: {
    printf("STY $%02X,X", READ_U8());
  } break;
  case 0x95: {
    printf("STA $%02X,X", READ_U8());
  } break;
  case 0x96: {
    printf("STX $%02X,Y", READ_U8());
  } break;
  case 0x98: {
    printf("TYA\t");
  } break;
  case 0x99: {
    printf("STA $%04X,Y", READ_U16());
  } break;
  case 0x9A: {
    printf("TXS\t");
  } break;
  case 0x9D: {
    printf("STA $%04X,X", READ_U16());
  } break;
  case 0xA0: {
    printf("LDY #$%02X\t", READ_U8());
  } break;
  case 0xA1: {
    printf("LDA ($%02X,X)", READ_U8());
  } break;
  case 0xA2: {
    printf("LDX #$%02X\t", READ_U8());
  } break;
  case 0xA4: {
    printf("LDY $%02X", READ_U8());
  } break;
  case 0xA5: {
    printf("LDA $%02X", READ_U8());
  } break;
  case 0xA6: {
    printf("LDX $%02X", READ_U8());
  } break;
  case 0xA8: {
    printf("TAY\t");
  } break;
  case 0xA9: {
    printf("LDA #$%02X\t", READ_U8());
  } break;
  case 0xAA: {
    printf("TAX\t");
  } break;
  case 0xAC: {
    printf("LDY $%04X", READ_U16());
  } break;
  case 0xAD: {
    printf("LDA $%04X", READ_U16());
  } break;
  case 0xAE: {
    printf("LDX $%04X", READ_U16());
  } break;
  case 0xB0: {
    int8_t offset = READ_U8();
    printf("BCS $%04X", m->pc + offset);
  } break;
  case 0xB1: {
    printf("LDA ($%02X),Y", READ_U8());
  } break;
  case 0xB4: {
    printf("LDY $%02X,X", READ_U8());
  } break;
  case 0xB5: {
    printf("LDA $%02X,X", READ_U8());
  } break;
  case 0xB6: {
    printf("LDX $%02X,Y", READ_U8());
  } break;
  case 0xB8: {
    printf("CLV\t");
  } break;
  case 0xB9: {
    printf("LDA $%04X,Y", READ_U16());
  } break;
  case 0xBA: {
    printf("TSX\t");
  } break;
  case 0xBC: {
    printf("LDY $%04X,X", READ_U16());
  } break;
  case 0xBD: {
    printf("LDA $%04X,X", READ_U16());
  } break;
  case 0xBE: {
    printf("LDX $%04X,Y", READ_U16());
  } break;
  case 0xC0: {
    printf("CPY #$%02X", READ_U8());
  } break;
  case 0xC1: {
    printf("CMP ($%02X,X)", READ_U8());
  } break;
  case 0xC4: {
    printf("CPY $%02X", READ_U8());
  } break;
  case 0xC5: {
    printf("CMP $%02X", READ_U8());
  } break;
  case 0xC6: {
    printf("DEC $%02X", READ_U8());
  } break;
  case 0xC8: {
    printf("INY\t");
  } break;
  case 0xC9: {
    printf("CMP #$%02X\t", READ_U8());
  } break;
  case 0xCA: {
    printf("DEX\t");
  } break;
  case 0xCC: {
    printf("CPY $%04X", READ_U16());
  } break;
  case 0xCD: {
    printf("CMP $%04X", READ_U16());
  } break;
  case 0xCE: {
    printf("DEC $%04X", READ_U16());
  } break;
  case 0xD0: {
    int8_t offset = READ_U8();
    printf("BNE $%04X", m->pc + offset);
  } break;
  case 0xD1: {
    printf("CMP ($%02X),Y", READ_U8());
  } break;
  case 0xD5: {
    printf("CMP $%02X,X", READ_U8());
  } break;
  case 0xD6: {
    printf("DEC $%02X,X", READ_U8());
  } break;
  case 0xD8: {
    printf("CLD\t");
  } break;
  case 0xD9: {
    printf("CMP $%04X,Y", READ_U16());
  } break;
  case 0xDD: {
    printf("CMP $%04X,X", READ_U16());
  } break;
  case 0xDE: {
    printf("DEC $%04X,X", READ_U16());
  } break;
  case 0xE0: {
    printf("CPX #$%02X", READ_U8());
  } break;
  case 0xE1: {
    printf("SBC ($%02X,X)", READ_U8());
  } break;
  case 0xE4: {
    printf("CPX $%02X", READ_U8());
  } break;
  case 0xE5: {
    printf("SBC $%02X", READ_U8());
  } break;
  case 0xE6: {
    printf("INC $%02X", READ_U8());
  } break;
  case 0xE8: {
    printf("INX\t");
  } break;
  case 0xE9: {
    printf("SBC #$%02X\t", READ_U8());
  } break;
  case 0xEA: {
    printf("NOP\t");
  } break;
  case 0xEC: {
    printf("CPX $%04X", READ_U16());
  } break;
  case 0xED: {
    printf("SBC $%04X", READ_U16());
  } break;
  case 0xEE: {
    printf("INC $%04X", READ_U16());
  } break;
  case 0xF0: {
    int8_t offset = READ_U8();
    printf("BEQ $%04X", m->pc + offset);
  } break;
  case 0xF1: {
    printf("SBC ($%02X),Y", READ_U8());
  } break;
  case 0xF5: {
    printf("SBC $%02X,X", READ_U8());
  } break;
  case 0xF6: {
    printf("INC $%02X,X", READ_U8());
  } break;
  case 0xF8: {
    printf("SED\t");
  } break;
  case 0xF9: {
    printf("SBC $%04X,Y", READ_U16());
  } break;
  case 0xFD: {
    printf("SBC $%04X,X", READ_U16());
  } break;
  case 0xFE: {
    printf("INC $%04X,X", READ_U16());
  } break;
  default:
    printf("???\t");
  }
}

void mos6502_free(MOS6502 m) { free(m.memory); }

int main(int argc, char *argv[]) {
  const char *path = NULL;
  uint8_t disassemble = 0;
  uint8_t print_cpu = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-d") == 0) {
      disassemble = 1;
    } else if (strcmp(argv[i], "-p") == 0) {
      print_cpu = 1;
    } else {
      path = argv[i];
    }
  }

  if (path == NULL) {
    fprintf(stderr, "Usage: %s [-d] [-p] <path>\n", argv[0]);
    return 1;
  }

  MOS6502 m = mos6502_create();

  uint8_t buffer[1 << 16] = {0};
  FILE *f = fopen(path, "rb");
  size_t n = fread(buffer, 1, 1 << 16, f);
  fclose(f);

  for (size_t i = 0; i < n; i++) {
    m.memory[0x600 + i] = buffer[i];
  }

  if (disassemble) {
    while (m.pc < 0x600 + n) {
      printf("$%04X: ", m.pc);
      mos6502_disassemble(&m);
      printf("\n");
    }
  } else {
    while (1) {
      if (print_cpu) {
        printf("$%04X: ", m.pc);
        uint16_t old_pc = m.pc;
        mos6502_disassemble(&m);
        m.pc = old_pc;
        printf("\tA: %02X X: %02X Y: %02X\n", m.A, m.X, m.Y);
      }

      mos6502_step(&m);

      if (m.display_modified_this_cycle) {
        static char buf[1024];
        size_t p = 0;
        for (size_t y = 0; y < 16; y++) {
          for (size_t x = 0; x < 32; x++) {
            char c = mos6502_mem_read(&m, 0x0400 + x + (y * 32));
            buf[p++] = c == 0 ? ' ' : c;
          }
          buf[p++] = '\n';
        }
        buf[p] = '\0';
        printf("\x1b[?25l%s\x1b[8A\x1b[8A", buf);
      }
    }
  }

  mos6502_free(m);
}
