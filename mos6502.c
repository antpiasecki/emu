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
  uint8_t A;
  uint8_t X;
  uint8_t Y;
  uint8_t SP;
  uint8_t P;
} MOS6502;

MOS6502 mos6502_create() {
  MOS6502 m = {0};
  m.pc = 0x600;
  m.memory = malloc(1 << 16);
  return m;
}

void mos6502_disassemble(MOS6502 *m, size_t ins_count) {
  for (size_t i = 0; i < ins_count; i++) {
    uint8_t op = m->memory[m->pc++];
    switch (op) {
    case 0x00: {
      printf("BRK\n");
    } break;
    case 0x4C: {
      READ_ADDR();
      printf("JMP $%X\n", addr);
    } break;
    case 0x8D: {
      READ_ADDR();
      printf("STA $%X\n", addr);
    } break;
    case 0xA2: {
      printf("LDX #$%X\n", m->memory[m->pc++]);
    } break;
    case 0xBD: {
      READ_ADDR();
      printf("LDA $%X,X\n", addr);
    } break;
    case 0xE8: {
      printf("INX\n");
    } break;
    case 0xF0: {
      printf("BEQ %d\n", m->memory[m->pc++]);
    } break;
    default:
      fprintf(stderr, "unrecognized opcode: %x\n", op);
      exit(1);
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <path>\n", argv[0]);
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

  mos6502_disassemble(&m, 7);
}