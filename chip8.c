// http://devernay.free.fr/hacks/chip8/C8TECH10.HTM
#include <raylib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_INS()                                                             \
  uint8_t low = c->memory[c->pc++];                                            \
  uint8_t high = c->memory[c->pc++];                                           \
  uint16_t ins = (low << 8) | high;                                            \
  uint16_t nnn = ins & 0x0FFF;                                                 \
  uint8_t n = ins & 0x000F;                                                    \
  uint8_t x = (ins >> 8) & 0x0F;                                               \
  uint8_t y = (ins >> 4) & 0x0F;                                               \
  uint8_t kk = ins & 0x00FF

#define BAD_INS()                                                              \
  fprintf(stderr, "unrecognized instruction\n");                               \
  exit(1)

static const uint8_t keyboard_map[] = {KEY_ONE,  KEY_TWO, KEY_THREE, KEY_FOUR,
                                       KEY_FIVE, KEY_SIX, KEY_SEVEN, KEY_EIGHT,
                                       KEY_NINE, KEY_Q,   KEY_W,     KEY_E,
                                       KEY_R,    KEY_T,   KEY_Y,     KEY_U};

typedef struct CHIP8 {
  uint8_t *memory;
  uint16_t pc;
  uint16_t stack[16];
  uint8_t sp;
  uint8_t reg[16];
  uint16_t I;
  uint8_t delay_timer;
  uint8_t sound_timer;
  uint8_t keyboard[16];
  uint8_t *display;
} CHIP8;

CHIP8 chip8_create(void) {
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
  c.display = calloc(1, 64 * 32);
  for (size_t i = 0; i < 80; i++)
    c.memory[i] = fonts[i];
  c.pc = 0x200;
  return c;
}

void chip8_disassemble(CHIP8 *c, size_t ins_count) {
  for (size_t i = 0; i < ins_count; i++) {
    printf("0x%x: ", c->pc);

    READ_INS();

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
        printf("SYS %u\n", nnn);
      }
    } break;
    case 0x1: {
      printf("JP 0x%x\n", nnn);
    } break;
    case 0x2: {
      printf("CALL %u\n", nnn);
    } break;
    case 0x3: {
      printf("SE V%x, %u\n", x, kk);
    } break;
    case 0x4: {
      printf("SNE V%x, %u\n", x, kk);
    } break;
    case 0x5: {
      printf("SE V%x, V%x\n", x, y);
    } break;
    case 0x6: {
      printf("LD V%x, %u\n", x, kk);
    } break;
    case 0x7: {
      printf("ADD V%x, %u\n", x, kk);
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
        BAD_INS();
      }
    } break;
    case 0x9: {
      printf("SNE V%x, V%x\n", x, y);
    } break;
    case 0xA: {
      printf("LD I, 0x%x\n", nnn);
    } break;
    case 0xB: {
      printf("JP V0, %u\n", nnn);
    } break;
    case 0xC: {
      printf("RND V%x, %u\n", x, kk);
    } break;
    case 0xD: {
      printf("DRW V%x, V%x, %u\n", x, y, n);
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
        BAD_INS();
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
        BAD_INS();
      }
    } break;
    default:
      BAD_INS();
    }
  }
}

void chip8_step(CHIP8 *c) {
  READ_INS();

  switch ((ins >> 12) & 0xF) {
  case 0x0: {
    switch (nnn) {
    case 0x0E0:
      memset(c->display, 0, 64 * 32);
      break;
    case 0x0EE:
      c->pc = c->stack[c->sp];
      c->sp--;
      break;
    default:
      // >This instruction is only used on the old computers on which Chip-8
      // >was originally implemented. It is ignored by modern interpreters.
      break;
    }
  } break;
  case 0x1: {
    c->pc = nnn;
  } break;
  case 0x2: {
    c->sp++;
    c->stack[c->sp] = c->pc;
    c->pc = nnn;
  } break;
  case 0x3: {
    if (c->reg[x] == kk) {
      c->pc += 2;
    }
  } break;
  case 0x4: {
    if (c->reg[x] != kk) {
      c->pc += 2;
    }
  } break;
  case 0x5: {
    if (c->reg[x] == c->reg[y]) {
      c->pc += 2;
    }
  } break;
  case 0x6: {
    c->reg[x] = kk;
  } break;
  case 0x7: {
    c->reg[x] += kk;
  } break;
  case 0x8: {
    switch (n) {
    case 0x0:
      c->reg[x] = c->reg[y];
      break;
    case 0x1:
      c->reg[x] |= c->reg[y];
      break;
    case 0x2:
      c->reg[x] &= c->reg[y];
      break;
    case 0x3:
      c->reg[x] ^= c->reg[y];
      break;
    case 0x4: {
      uint16_t res = (uint16_t)(c->reg[x]) + (uint16_t)c->reg[y];
      c->reg[0xF] = res > 0xFF;
      c->reg[x] = (uint8_t)(res & 0xFF);
    } break;
    case 0x5:
      c->reg[0xF] = c->reg[x] > c->reg[y];
      c->reg[x] -= c->reg[y];
      break;
    case 0x6:
      c->reg[0xF] = c->reg[x] & 0x1;
      c->reg[x] >>= 1;
      break;
    case 0x7:
      c->reg[0xF] = c->reg[y] > c->reg[x];
      c->reg[x] = c->reg[y] - c->reg[x];
      break;
    case 0xE:
      c->reg[0xF] = (c->reg[x] & 0x80) >> 7;
      c->reg[x] <<= 1;
      break;
    default:
      BAD_INS();
    }
  } break;
  case 0x9: {
    if (c->reg[x] != c->reg[y]) {
      c->pc += 2;
    }
  } break;
  case 0xA: {
    c->I = nnn;
  } break;
  case 0xB: {
    c->pc = nnn + c->reg[0];
  } break;
  case 0xC: {
    c->reg[x] = (rand() % 256) & kk;
  } break;
  case 0xD: {
    c->reg[0xF] = 0;
    for (size_t row = 0; row < n; row++) {
      for (int col = 0; col < 8; col++) {
        if ((c->memory[c->I + row] & (0x80 >> col)) != 0) {
          size_t pixel_x = (c->reg[x] + col) % 64;
          size_t pixel_y = (c->reg[y] + row) % 32;
          size_t offset = pixel_x + (pixel_y * 64);

          if (c->display[offset] == 1) {
            c->reg[0xF] = 1;
          }
          c->display[offset] ^= 1;
        }
      }
    }
  } break;
  case 0xE: {
    switch (kk) {
    case 0x9E:
      if (IsKeyDown(keyboard_map[c->reg[x]])) {
        c->pc += 2;
      }
      break;
    case 0xA1:
      if (!IsKeyDown(keyboard_map[c->reg[x]])) {
        c->pc += 2;
      }
      break;
    default:
      BAD_INS();
    }
  } break;
  case 0xF: {
    switch (kk) {
    case 0x07:
      c->reg[x] = c->delay_timer;
      break;
    case 0x0A: {
      bool key_pressed = false;
      while (!key_pressed && WindowShouldClose()) {
        for (size_t i = 0; i < 16; i++) {
          if (IsKeyDown(keyboard_map[i])) {
            key_pressed = true;
            break;
          }
        }
      }
    } break;
    case 0x15:
      c->delay_timer = c->reg[x];
      break;
    case 0x18:
      c->sound_timer = c->reg[x];
      break;
    case 0x1E:
      c->I += c->reg[x];
      break;
    case 0x29:
      c->I = c->reg[x] * 5;
      break;
    case 0x33:
      c->memory[c->I] = c->reg[x] / 100;
      c->memory[c->I + 1] = (c->reg[x] / 10) % 10;
      c->memory[c->I + 2] = c->reg[x] % 10;
      break;
    case 0x55:
      for (size_t i = 0; i <= x; i++) {
        c->memory[c->I + i] = c->reg[i];
      }
      break;
    case 0x65:
      for (size_t i = 0; i <= x; i++) {
        c->reg[i] = c->memory[c->I + i];
      }
      break;
    default:
      BAD_INS();
    }
  } break;
  default:
    BAD_INS();
  }
}

void chip8_free(CHIP8 c) {
  free(c.memory);
  free(c.display);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <path>\n", argv[0]);
    return 1;
  }

  CHIP8 c = chip8_create();

  uint8_t buffer[4000];
  FILE *f = fopen(argv[1], "rb");
  size_t n = fread(buffer, 1, 4000, f);
  fclose(f);

  for (size_t i = 0; i < n; i++) {
    c.memory[0x200 + i] = buffer[i];
  }

  InitWindow(640, 320, "CHIP-8");
  SetTargetFPS(60);

  while (!WindowShouldClose()) {
    if (c.delay_timer > 0) {
      c.delay_timer--;
    }
    if (c.sound_timer > 0) {
      c.sound_timer--;
      // TODO: buzzer
    }

    for (int i = 0; i < 25; i++) {
      chip8_step(&c);
    }

    BeginDrawing();

    ClearBackground(WHITE);
    for (size_t y = 0; y < 32; y++) {
      for (size_t x = 0; x < 64; x++) {
        if (c.display[x + y * 64] == 1) {
          DrawRectangle(x * 10, y * 10, 10, 10, BLACK);
        }
      }
    }

    EndDrawing();
  }
}
