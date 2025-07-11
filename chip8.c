// http://devernay.free.fr/hacks/chip8/C8TECH10.HTM
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct CHIP8 {
  uint8_t *memory;
  uint16_t pc;
  uint16_t stack[16];
  uint8_t sp;
  uint8_t reg[16];
  uint16_t I;
  uint8_t delay_timer;
  uint8_t sound_timer;
} CHIP8;

CHIP8 chip8_create() {
  static const uint8_t fonts[] = {
      0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
      0x20, 0x60, 0x20, 0x20, 0x70, // 1
      0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
      0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
      0x90, 0x90, 0xF0, 0x10, 0x10, // 4
      0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
      0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
      0xF0, 0x10, 0x20, 0x40, 0x40, // 7
      0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
      0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
      0xF0, 0x90, 0xF0, 0x90, 0x90, // A
      0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
      0xF0, 0x80, 0x80, 0x80, 0xF0, // C
      0xE0, 0x90, 0x90, 0x90, 0xE0, // D
      0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
      0xF0, 0x80, 0xF0, 0x80, 0x80, // F
  };
  CHIP8 c = {0};
  c.memory = calloc(1, 4096);
  for (size_t i = 0; i < 80; i++)
    c.memory[i] = fonts[i];
  c.pc = 0x200;
  return c;
}

void chip8_disassemble(CHIP8 *c, size_t size) {
  for (size_t i = 0; i < size; i++) {
    uint8_t low = c->memory[c->pc++];
    uint8_t high = c->memory[c->pc++];
    uint16_t ins = (low << 8) | high;

    uint16_t nnn = ins & 0x0FFF;
    uint8_t n = ins & 0x000F;
    uint8_t x = (ins >> 8) & 0x0F;
    uint8_t y = (ins >> 4) & 0x0F;
    uint8_t kk = ins & 0x00FF;

    switch ((ins >> 12) & 0xF) {
    case 0x0: {
      switch (nnn) {
      case 0x0E0:
        puts("CLS");
        break;
      case 0x0EE:
        puts("RET");
        break;
      default:
        printf("SYS %hd\n", nnn);
      }
    } break;
    case 0x1: {
      printf("JP %hd\n", nnn);
    } break;
    case 0x2: {
      printf("CALL %hd\n", nnn);
    } break;
    case 0x3: {
      printf("SE V%x, %hhd\n", x, kk);
    } break;
    case 0x4: {
      printf("SNE V%x, %hhd\n", x, kk);
    } break;
    case 0x5: {
      printf("SE V%x, V%x\n", x, y);
    } break;
    case 0x6: {
      printf("LD V%x, %hhd\n", x, kk);
    } break;
    case 0x7: {
      printf("ADD V%x, %hhd\n", x, kk);
    } break;
    case 0x8: {
      switch (n) {
      case 0x0:
        printf("LD V%x, V%x\n", x, y);
        break;
      case 0x1:
        printf("OR V%x, V%x\n", x, y);
        break;
      case 0x2:
        printf("AND V%x, V%x\n", x, y);
        break;
      case 0x3:
        printf("XOR V%x, V%x\n", x, y);
        break;
      case 0x4:
        printf("ADD V%x, V%x\n", x, y);
        break;
      case 0x5:
        printf("SUB V%x, V%x\n", x, y);
        break;
      case 0x6:
        printf("SHR V%x\n", x);
        break;
      case 0x7:
        printf("SUBN V%x, V%x\n", x, y);
        break;
      case 0xE:
        printf("SHL V%x\n", x);
        break;
      default:
        fprintf(stderr, "unrecognized instruction\n");
        exit(1);
      }
    } break;
    case 0x9: {
      printf("SNE V%x, V%x\n", x, y);
    } break;
    case 0xA: {
      printf("LD I, %hd\n", nnn);
    } break;
    case 0xB: {
      printf("JP V0, %hd\n", nnn);
    } break;
    case 0xC: {
      printf("RND V%x, %hd\n", x, kk);
    } break;
    case 0xD: {
      printf("DRW V%x, V%x, %hhd\n", x, y, n);
    } break;
    case 0xE: {
      switch (kk) {
      case 0x9E:
        printf("SKP V%x\n", x);
        break;
      case 0xA1:
        printf("SKNP V%x\n", x);
        break;
      default:
        fprintf(stderr, "unrecognized instruction\n");
        exit(1);
      }
    } break;
    case 0xF: {
      switch (kk) {
      case 0x07:
        printf("LD V%x, DT\n", x);
        break;
      case 0x0A:
        printf("LD V%x, K\n", x);
        break;
      case 0x15:
        printf("LD DT, V%x\n", x);
        break;
      case 0x18:
        printf("LD ST, V%x\n", x);
        break;
      case 0x1E:
        printf("ADD I, V%x\n", x);
        break;
      case 0x29:
        printf("LD F, V%x\n", x);
        break;
      case 0x33:
        printf("LD B, V%x\n", x);
        break;
      case 0x55:
        printf("LD [I], V%x\n", x);
        break;
      case 0x65:
        printf("LD V%x, [I]\n", x);
        break;
      default:
        fprintf(stderr, "unrecognized instruction\n");
        exit(1);
      }
    } break;
    default:
      fprintf(stderr, "unrecognized instruction\n");
      exit(1);
    }
  }
}

int main(int argc, char *argv[]) {
  CHIP8 c = chip8_create();

  uint8_t buffer[100000];
  FILE *f = fopen(argv[1], "rb");
  size_t n = fread(buffer, 1, 100000, f);
  fclose(f);

  for (size_t i = 0; i < n; i++) {
    c.memory[0x200 + i] = buffer[i];
  }

  chip8_disassemble(&c, n);
}