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

MOS6502 mos6502_create() {
  MOS6502 m = {0};
  m.pc = 0x600;
  m.memory = calloc(1, 1 << 16);
  return m;
}

void mos6502_disassemble(MOS6502 *m, size_t ins_count) {
  for (size_t i = 0; i < ins_count; i++) {
    printf("%04X: ", m->pc);

    uint8_t op = m->memory[m->pc++];
    switch (op) {
    case 0x00: {
      printf("BRK\n");
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
    case 0x10: {
      int8_t offset = m->memory[m->pc++];
      printf("BPL $%04X\n", m->pc + offset);
    } break;
    case 0x18: {
      printf("CLC\n");
    } break;
    case 0x20: {
      READ_ADDR();
      printf("JSR $%04X\n", addr);
    } break;
    case 0x28: {
      printf("PLP\n");
    } break;
    case 0x29: {
      printf("AND #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0x2C: {
      READ_ADDR();
      printf("BIT $%04X\n", addr);
    } break;
    case 0x2E: {
      READ_ADDR();
      printf("ROL $%04X\n", addr);
    } break;
    case 0x30: {
      int8_t offset = m->memory[m->pc++];
      printf("BMI $%04X\n", m->pc + offset);
    } break;
    case 0x38: {
      printf("SEC\n");
    } break;
    case 0x40: {
      printf("RTI\n");
    } break;
    case 0x4C: {
      READ_ADDR();
      printf("JMP $%04X\n", addr);
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
    case 0x50: {
      int8_t offset = m->memory[m->pc++];
      printf("BVC $%04X\n", m->pc + offset);
    } break;
    case 0x60: {
      printf("RTS\n");
    } break;
    case 0x68: {
      printf("PLA\n");
    } break;
    case 0x69: {
      printf("ADC #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0x6E: {
      READ_ADDR();
      printf("ROR $%04X\n", addr);
    } break;
    case 0x70: {
      int8_t offset = m->memory[m->pc++];
      printf("BVS $%04X\n", m->pc + offset);
    } break;
    case 0x78: {
      printf("SEI\n");
    } break;
    case 0x7D: {
      READ_ADDR();
      printf("ADC $%04X,X\n", addr);
    } break;
    case 0x85: {
      printf("STA $%02X\n", m->memory[m->pc++]);
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
    case 0xA8: {
      printf("TAY\n");
    } break;
    case 0xA9: {
      printf("LDA #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0xAA: {
      printf("TAX\n");
    } break;
    case 0xAD: {
      READ_ADDR();
      printf("LDA $%04X\n", addr);
    } break;
    case 0xB0: {
      int8_t offset = m->memory[m->pc++];
      printf("BCS $%04X\n", m->pc + offset);
    } break;
    case 0xB1: {
      printf("LDA ($%02X,Y)\n", m->memory[m->pc++]);
    } break;
    case 0xB8: {
      printf("CLV\n");
    } break;
    case 0xBA: {
      printf("TSX\n");
    } break;
    case 0xBD: {
      READ_ADDR();
      printf("LDA $%04X,X\n", addr);
    } break;
    case 0xC0: {
      printf("CPY #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0xCA: {
      printf("DEX\n");
    } break;
    case 0xC8: {
      printf("INY\n");
    } break;
    case 0xC9: {
      printf("CMP #$%02X\n", m->memory[m->pc++]);
    } break;
    case 0xCE: {
      READ_ADDR();
      printf("DEC $%04X\n", addr);
    } break;
    case 0xD0: {
      int8_t offset = m->memory[m->pc++];
      printf("BNE $%04X\n", m->pc + offset);
    } break;
    case 0xD8: {
      printf("CLD\n");
    } break;
    case 0xE0: {
      printf("CPX #$%02X\n", m->memory[m->pc++]);
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
    case 0xEE: {
      READ_ADDR();
      printf("INC $%04X\n", addr);
    } break;
    case 0xF0: {
      int8_t offset = m->memory[m->pc++];
      printf("BEQ $%04X\n", m->pc + offset);
    } break;
    case 0xF8: {
      printf("SED\n");
    } break;
    default:
      fprintf(stderr, "unrecognized opcode: %02X\n", op);
      exit(1);
    }
  }
}

void mos6502_free(MOS6502 m) { free(m.memory); }

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <path> <ins count>\n", argv[0]);
    return 1;
  }

  MOS6502 m = mos6502_create();

  uint8_t buffer[4000] = {0};
  FILE *f = fopen(argv[1], "rb");
  size_t n = fread(buffer, 1, 4000, f);
  fclose(f);

  for (size_t i = 0; i < n; i++) {
    m.memory[0x600 + i] = buffer[i];
  }

  mos6502_disassemble(&m, atoi(argv[2]));

  mos6502_free(m);
}