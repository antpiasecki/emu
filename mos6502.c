// https://www.masswerk.at/6502/6502_instruction_set.html
// https://tutorial-6502.sourceforge.io/specification/opcodes/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define READ_ADDR()                                                            \
  uint16_t low = m->memory[m->pc++];                                           \
  uint16_t high = m->memory[m->pc++];                                          \
  uint16_t addr = (high << 8) | low

typedef struct MOS6502 {
  uint8_t *memory;
  uint16_t pc;
  // TODO
  // uint8_t A;
  // uint8_t X;
  // uint8_t Y;
  // uint8_t SP;
  // uint8_t P;
} MOS6502;

MOS6502 mos6502_create(void) {
  MOS6502 m = {0};
  m.pc = 0x600;
  m.memory = calloc(1, 1 << 16);
  return m;
}

void mos6502_disassemble(MOS6502 *m, size_t program_size) {
  while (m->pc < 0x600 + program_size) {
    printf("%04X: ", m->pc);

    uint8_t op = m->memory[m->pc++];
    switch (op) {
    case 0x00: {
      printf("BRK\n");
    } break;
    case 0x01: {
      printf("ORA ($%02X,X)\n", m->memory[m->pc++]);
    } break;
    case 0x05: {
      printf("ORA $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x06: {
      printf("ASL $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x08: {
      printf("PHP\n");
    } break;
    case 0x09: {
      printf("ORA #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0x0A: {
      printf("ASL A\n");
    } break;
    case 0x0D: {
      READ_ADDR();
      printf("ORA $%04X\n", addr);
    } break;
    case 0x0E: {
      READ_ADDR();
      printf("ASL $%04X\n", addr);
    } break;
    case 0x10: {
      int8_t offset = m->memory[m->pc++];
      printf("BPL $%04X\n", m->pc + offset);
    } break;
    case 0x11: {
      printf("ORA ($%02X),Y\n", m->memory[m->pc++]);
    } break;
    case 0x15: {
      printf("ORA $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0x16: {
      printf("ASL $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0x18: {
      printf("CLC\n");
    } break;
    case 0x19: {
      READ_ADDR();
      printf("ORA $%04X,Y\n", addr);
    } break;
    case 0x1D: {
      READ_ADDR();
      printf("ORA $%04X,X\n", addr);
    } break;
    case 0x1E: {
      READ_ADDR();
      printf("ASL $%04X,X\n", addr);
    } break;
    case 0x20: {
      READ_ADDR();
      printf("JSR $%04X\n", addr);
    } break;
    case 0x21: {
      printf("AND ($%02X,X)\n", m->memory[m->pc++]);
    } break;
    case 0x24: {
      printf("BIT $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x25: {
      printf("AND $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x26: {
      printf("ROL $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x28: {
      printf("PLP\n");
    } break;
    case 0x29: {
      printf("AND #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0x2A: {
      printf("ROL A\n");
    } break;
    case 0x2C: {
      READ_ADDR();
      printf("BIT $%04X\n", addr);
    } break;
    case 0x2D: {
      READ_ADDR();
      printf("AND $%04X\n", addr);
    } break;
    case 0x2E: {
      READ_ADDR();
      printf("ROL $%04X\n", addr);
    } break;
    case 0x30: {
      int8_t offset = m->memory[m->pc++];
      printf("BMI $%04X\n", m->pc + offset);
    } break;
    case 0x31: {
      printf("AND ($%02X),Y\n", m->memory[m->pc++]);
    } break;
    case 0x35: {
      printf("AND $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0x36: {
      printf("ROL $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0x38: {
      printf("SEC\n");
    } break;
    case 0x39: {
      READ_ADDR();
      printf("AND $%04X,Y\n", addr);
    } break;
    case 0x3D: {
      READ_ADDR();
      printf("AND $%04X,X\n", addr);
    } break;
    case 0x3E: {
      READ_ADDR();
      printf("ROL $%04X,X\n", addr);
    } break;
    case 0x40: {
      printf("RTI\n");
    } break;
    case 0x41: {
      printf("EOR ($%02X,X)\n", m->memory[m->pc++]);
    } break;
    case 0x45: {
      printf("EOR $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x46: {
      printf("LSR $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x48: {
      printf("PHA\n");
    } break;
    case 0x49: {
      printf("EOR #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0x4A: {
      printf("LSR A\n");
    } break;
    case 0x4C: {
      READ_ADDR();
      printf("JMP $%04X\n", addr);
    } break;
    case 0x4D: {
      READ_ADDR();
      printf("EOR $%04X\n", addr);
    } break;
    case 0x4E: {
      READ_ADDR();
      printf("LSR $%04X\n", addr);
    } break;
    case 0x50: {
      int8_t offset = m->memory[m->pc++];
      printf("BVC $%04X\n", m->pc + offset);
    } break;
    case 0x51: {
      printf("EOR ($%02X),Y\n", m->memory[m->pc++]);
    } break;
    case 0x55: {
      printf("EOR $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0x56: {
      printf("LSR $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0x58: {
      printf("CLI\n");
    } break;
    case 0x59: {
      READ_ADDR();
      printf("EOR $%04X,Y\n", addr);
    } break;
    case 0x5D: {
      READ_ADDR();
      printf("EOR $%04X,X\n", addr);
    } break;
    case 0x5E: {
      READ_ADDR();
      printf("LSR $%04X,X\n", addr);
    } break;
    case 0x60: {
      printf("RTS\n");
    } break;
    case 0x61: {
      printf("ADC ($%02X,X)\n", m->memory[m->pc++]);
    } break;
    case 0x65: {
      printf("ADC $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x66: {
      printf("ROR $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x68: {
      printf("PLA\n");
    } break;
    case 0x69: {
      printf("ADC #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0x6A: {
      printf("ROR A\n");
    } break;
    case 0x6C: {
      READ_ADDR();
      printf("JMP ($%04X)\n", addr);
    } break;
    case 0x6D: {
      READ_ADDR();
      printf("ADC $%04X\n", addr);
    } break;
    case 0x6E: {
      READ_ADDR();
      printf("ROR $%04X\n", addr);
    } break;
    case 0x70: {
      int8_t offset = m->memory[m->pc++];
      printf("BVS $%04X\n", m->pc + offset);
    } break;
    case 0x71: {
      printf("ADC ($%02X),Y\n", m->memory[m->pc++]);
    } break;
    case 0x75: {
      printf("ADC $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0x76: {
      printf("ROR $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0x78: {
      printf("SEI\n");
    } break;
    case 0x79: {
      READ_ADDR();
      printf("ADC $%04X,Y\n", addr);
    } break;
    case 0x7E: {
      READ_ADDR();
      printf("ROR $%04X,X\n", addr);
    } break;
    case 0x7D: {
      READ_ADDR();
      printf("ADC $%04X,X\n", addr);
    } break;
    case 0x81: {
      printf("STA ($%02X,X)\n", m->memory[m->pc++]);
    } break;
    case 0x84: {
      printf("STY $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x85: {
      printf("STA $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x86: {
      printf("STX $%02X\n", m->memory[m->pc++]);
    } break;
    case 0x88: {
      printf("DEY\n");
    } break;
    case 0x8A: {
      printf("TXA\n");
    } break;
    case 0x8C: {
      READ_ADDR();
      printf("STY $%04X\n", addr);
    } break;
    case 0x8D: {
      READ_ADDR();
      printf("STA $%04X\n", addr);
    } break;
    case 0x8E: {
      READ_ADDR();
      printf("STX $%04X\n", addr);
    } break;
    case 0x90: {
      int8_t offset = m->memory[m->pc++];
      printf("BCC $%04X\n", m->pc + offset);
    } break;
    case 0x91: {
      printf("STA ($%02X),Y\n", m->memory[m->pc++]);
    } break;
    case 0x94: {
      printf("STY $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0x95: {
      printf("STA $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0x96: {
      printf("STX $%02X,Y\n", m->memory[m->pc++]);
    } break;
    case 0x98: {
      printf("TYA\n");
    } break;
    case 0x99: {
      READ_ADDR();
      printf("STA $%04X,Y\n", addr);
    } break;
    case 0x9A: {
      printf("TXS\n");
    } break;
    case 0x9D: {
      READ_ADDR();
      printf("STA $%04X,X\n", addr);
    } break;
    case 0xA0: {
      printf("LDY #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0xA1: {
      printf("LDA ($%02X,X)\n", m->memory[m->pc++]);
    } break;
    case 0xA2: {
      printf("LDX #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0xA4: {
      printf("LDY $%02X\n", m->memory[m->pc++]);
    } break;
    case 0xA5: {
      printf("LDA $%02X\n", m->memory[m->pc++]);
    } break;
    case 0xA6: {
      printf("LDX $%02X\n", m->memory[m->pc++]);
    } break;
    case 0xA8: {
      printf("TAY\n");
    } break;
    case 0xA9: {
      printf("LDA #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0xAA: {
      printf("TAX\n");
    } break;
    case 0xAC: {
      READ_ADDR();
      printf("LDY $%04X\n", addr);
    } break;
    case 0xAD: {
      READ_ADDR();
      printf("LDA $%04X\n", addr);
    } break;
    case 0xAE: {
      READ_ADDR();
      printf("LDX $%04X\n", addr);
    } break;
    case 0xB0: {
      int8_t offset = m->memory[m->pc++];
      printf("BCS $%04X\n", m->pc + offset);
    } break;
    case 0xB1: {
      printf("LDA ($%02X,Y)\n", m->memory[m->pc++]);
    } break;
    case 0xB4: {
      printf("LDY $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0xB5: {
      printf("LDA $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0xB6: {
      printf("LDX $%02X,Y\n", m->memory[m->pc++]);
    } break;
    case 0xB8: {
      printf("CLV\n");
    } break;
    case 0xB9: {
      READ_ADDR();
      printf("LDA $%04X,Y\n", addr);
    } break;
    case 0xBA: {
      printf("TSX\n");
    } break;
    case 0xBC: {
      READ_ADDR();
      printf("LDY $%04X,X\n", addr);
    } break;
    case 0xBD: {
      READ_ADDR();
      printf("LDA $%04X,X\n", addr);
    } break;
    case 0xBE: {
      READ_ADDR();
      printf("LDX $%04X,Y\n", addr);
    } break;
    case 0xC0: {
      printf("CPY #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0xC1: {
      printf("CMP ($%02X,X)\n", m->memory[m->pc++]);
    } break;
    case 0xC4: {
      printf("CPY $%02X\n", m->memory[m->pc++]);
    } break;
    case 0xC5: {
      printf("CMP $%02X\n", m->memory[m->pc++]);
    } break;
    case 0xC6: {
      printf("DEC $%02X\n", m->memory[m->pc++]);
    } break;
    case 0xC8: {
      printf("INY\n");
    } break;
    case 0xC9: {
      printf("CMP #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0xCA: {
      printf("DEX\n");
    } break;
    case 0xCC: {
      READ_ADDR();
      printf("CPY $%04X\n", addr);
    } break;
    case 0xCD: {
      READ_ADDR();
      printf("CMP $%04X\n", addr);
    } break;
    case 0xCE: {
      READ_ADDR();
      printf("DEC $%04X\n", addr);
    } break;
    case 0xD0: {
      int8_t offset = m->memory[m->pc++];
      printf("BNE $%04X\n", m->pc + offset);
    } break;
    case 0xD1: {
      printf("CMP ($%02X),Y\n", m->memory[m->pc++]);
    } break;
    case 0xD5: {
      printf("CMP $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0xD6: {
      printf("DEC $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0xD8: {
      printf("CLD\n");
    } break;
    case 0xD9: {
      READ_ADDR();
      printf("CMP $%04X,Y\n", addr);
    } break;
    case 0xDD: {
      READ_ADDR();
      printf("CMP $%04X,X\n", addr);
    } break;
    case 0xDE: {
      READ_ADDR();
      printf("DEC $%04X,X\n", addr);
    } break;
    case 0xE0: {
      printf("CPX #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0xE1: {
      printf("SBC ($%02X,X)\n", m->memory[m->pc++]);
    } break;
    case 0xE4: {
      printf("CPX $%02X\n", m->memory[m->pc++]);
    } break;
    case 0xE5: {
      printf("SBC $%02X\n", m->memory[m->pc++]);
    } break;
    case 0xE6: {
      printf("INC $%02X\n", m->memory[m->pc++]);
    } break;
    case 0xE8: {
      printf("INX\n");
    } break;
    case 0xE9: {
      printf("SBC #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0xEA: {
      printf("NOP\n");
    } break;
    case 0xEC: {
      READ_ADDR();
      printf("CPX $%04X\n", addr);
    } break;
    case 0xED: {
      READ_ADDR();
      printf("SBC $%04X\n", addr);
    } break;
    case 0xEE: {
      READ_ADDR();
      printf("INC $%04X\n", addr);
    } break;
    case 0xF0: {
      int8_t offset = m->memory[m->pc++];
      printf("BEQ $%04X\n", m->pc + offset);
    } break;
    case 0xF1: {
      printf("SBC ($%02X),Y\n", m->memory[m->pc++]);
    } break;
    case 0xF5: {
      printf("SBC $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0xF6: {
      printf("INC $%02X,X\n", m->memory[m->pc++]);
    } break;
    case 0xF8: {
      printf("SED\n");
    } break;
    case 0xF9: {
      READ_ADDR();
      printf("SBC $%04X,Y\n", addr);
    } break;
    case 0xFD: {
      READ_ADDR();
      printf("SBC $%04X,X\n", addr);
    } break;
    case 0xFE: {
      READ_ADDR();
      printf("INC $%04X,X\n", addr);
    } break;
    default:
      fprintf(stderr, "unrecognized opcode: %02X\n", op);
      exit(1);
    }
  }
}

void mos6502_free(MOS6502 m) { free(m.memory); }

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <path>\n", argv[0]);
    return 1;
  }

  MOS6502 m = mos6502_create();

  uint8_t buffer[1 << 16] = {0};
  FILE *f = fopen(argv[1], "rb");
  size_t n = fread(buffer, 1, 1 << 16, f);
  fclose(f);

  for (size_t i = 0; i < n; i++) {
    m.memory[0x600 + i] = buffer[i];
  }

  mos6502_disassemble(&m, n);

  mos6502_free(m);
}
