// https://docs.riscv.org/reference/isa/_attachments/riscv-unprivileged.pdf
// https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf
#include "riscv64_dis.h"
#include <gelf.h>
#include <libelf.h>

const char *const REGS[32] = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "fp", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

Section riscv64_get_code_section(uint8_t *exe_bytes, size_t exe_size) {
  Elf *elf = elf_memory((char *)exe_bytes, exe_size);
  if (!elf) {
    fprintf(stderr, "elf_begin failed: %s\n", elf_errmsg(-1));
    exit(1);
  }

  size_t str_table_index;
  if (elf_getshdrstrndx(elf, &str_table_index) != 0) {
    fprintf(stderr, "elf_getshdrstrndx failed: %s\n", elf_errmsg(-1));
    exit(1);
  }

  GElf_Ehdr ehdr;
  if (gelf_getehdr(elf, &ehdr) == 0) {
    fprintf(stderr, "gelf_getehdr failed: %s\n", elf_errmsg(-1));
    exit(1);
  }

  Elf_Scn *section = NULL;
  while ((section = elf_nextscn(elf, section)) != NULL) {
    GElf_Shdr header;
    if (gelf_getshdr(section, &header) != &header)
      continue;

    const char *name = elf_strptr(elf, str_table_index, header.sh_name);
    if (name && strcmp(name, ".text") == 0) {
      elf_end(elf);
      return (Section){.offset = header.sh_offset,
                       .size = header.sh_size,
                       .entrypoint =
                           ehdr.e_entry - header.sh_addr + header.sh_offset};
    }
  }

  fprintf(stderr, "Failed to locate .text\n");
  exit(1);
}

RISCV64 riscv64_load(uint8_t *exe_bytes, size_t exe_size) {
  Section section = riscv64_get_code_section(exe_bytes, exe_size);

  return (RISCV64){
      .memory = exe_bytes,
      .pc = section.offset,
      .regs = {0},
      .code_section = section,
  };
}

void riscv64_dump(const RISCV64 *r) {
  printf("REGS:");
  for (size_t i = 0; i < 32; i++) {
    printf(" %lu", r->regs[i]);
  }
  printf("\n");
}

void riscv64_execute(RISCV64 *r) {
  r->pc = r->code_section.entrypoint;

  r->regs[2] = (20 * 1024 * 1024) - 1; // set the stack pointer

  while (r->pc < r->code_section.offset + r->code_section.size) {
    r->regs[0] = 0; // clear the zero register

    uint32_t ins;
    memcpy(&ins, r->memory + r->pc, 4);

    uint16_t opcode = ins & 0b1111111;

    // https://stackoverflow.com/questions/62939410/how-can-i-find-out-the-instruction-format-of-a-risc-v-instruction
    switch (opcode) {
    case 0b1100011: {
      PARSE_B_INS(ins);

      if (funct3 == 0b000) { // beq
        if (r->regs[rs1] == r->regs[rs2]) {
          r->pc += imm;
          continue;
        }
      } else if (funct3 == 0b001) { // bne
        if (r->regs[rs1] != r->regs[rs2]) {
          r->pc += imm;
          continue;
        }
      } else if (funct3 == 0b101) { // bge
        if (r->regs[rs1] >= r->regs[rs2]) {
          r->pc += imm;
          continue;
        }
      } else if (funct3 == 0b100) { // blt
        if (r->regs[rs1] < r->regs[rs2]) {
          r->pc += imm;
          continue;
        }
      } else if (funct3 == 0b110) { // bltu
        if ((uint64_t)r->regs[rs1] < (uint64_t)r->regs[rs2]) {
          r->pc += imm;
          continue;
        }
      } else {
        fprintf(stderr, "B-type: unrecognized funct3: %03b\n", funct3);
        exit(1);
      }
    }; break;
    case 0b0010011: {
      PARSE_I_INS(ins);

      if (funct3 == 0b000) { // addi
        r->regs[rd] = r->regs[rs1] + imm;
      } else if (funct3 == 0b001) { // slli
        uint32_t shamt = (ins >> 20) & 0b111111;
        uint32_t funct6 = (ins >> 26) & 0b111111;

        if (funct6 == 0b000000) {
          r->regs[rd] = r->regs[rs1] << shamt;
        } else {
          fprintf(stderr, "I-type 1: funct3=001: unrecognized funct6: %b\n",
                  funct6);
          exit(1);
        }
      } else if (funct3 == 0b011) { // sltiu
        r->regs[rd] = (r->regs[rs1] < (uint64_t)(int64_t)imm) ? 1 : 0;
      } else if (funct3 == 0b100) { // xori
        r->regs[rd] = r->regs[rs1] ^ imm;
      } else if (funct3 == 0b101) {
        // of course this one just has to be different
        uint32_t shamt = (ins >> 20) & 0b111111;
        uint32_t funct6 = (ins >> 26) & 0b111111;

        if (funct6 == 0b000000) { // srli
          r->regs[rd] = r->regs[rs1] >> shamt;
        } else if (funct6 == 0b010000) { // srai
          r->regs[rd] = (int32_t)r->regs[rs1] >> shamt;
        } else {
          fprintf(stderr, "I-type 1: funct3=101: unrecognized funct6: %b\n",
                  funct6);
          exit(1);
        }
      } else if (funct3 == 0b111) {
        r->regs[rd] = r->regs[rs1] & imm;
      } else {
        fprintf(stderr, "I-type 1: unrecognized funct3: %03b\n", funct3);
        exit(1);
      }
    }; break;
    case 0b0000011: {
      PARSE_I_INS(ins);

      if (funct3 == 0b000) { // lb
        r->regs[rd] = (int8_t)r->memory[r->regs[rs1] + imm];
      } else if (funct3 == 0b001) { // lh
        r->regs[rd] = *(int16_t *)&r->memory[r->regs[rs1] + imm];
      } else if (funct3 == 0b010) { // lw
        r->regs[rd] = *(int32_t *)&r->memory[r->regs[rs1] + imm];
      } else if (funct3 == 0b011) { // ld
        r->regs[rd] = *(uint64_t *)&r->memory[r->regs[rs1] + imm];
      } else if (funct3 == 0b100) { // lbu
        r->regs[rd] = r->memory[r->regs[rs1] + imm];
      } else {
        fprintf(stderr, "I-type 2: unrecognized funct3: %03b\n", funct3);
        exit(1);
      }
    }; break;
    case 0b1100111: { // jalr
      PARSE_I_INS(ins);
      size_t target = (r->regs[rs1] + imm) & ~(size_t)1;
      r->regs[rd] = r->pc + 4;
      r->pc = target;
      continue;
    }; break;
    case 0b1110011: {
      PARSE_I_INS(ins);

      if (funct3 == 0b000) {
        if (imm == 0) { // ecall
          // https://jborza.com/post/2021-05-11-riscv-linux-syscalls/
          switch (r->regs[17]) {
          case 63: { // read
            if (r->regs[10] != 0) {
              fprintf(stderr, "read syscall implemented only for stdin.\n");
              exit(1);
            }

            size_t start = r->regs[11];
            size_t count = r->regs[12];
            size_t bytes_read = 0;

            for (size_t i = 0; i < count; i++) {
              int c = getchar();
              if (c == EOF)
                break;
              r->memory[start + i] = (uint8_t)c;
              bytes_read++;
              if (c == '\n')
                break;
            }

            r->regs[10] = bytes_read;
          }; break;
          case 64: { // write
            if (r->regs[10] != 1) {
              fprintf(stderr, "write syscall implemented only for stdout.\n");
              exit(1);
            }

            size_t start = r->regs[11];
            size_t end = r->regs[11] + r->regs[12];

            for (size_t i = start; i < end; i++) {
              putchar(r->memory[i]);
            }
          }; break;
          case 93: { // exit
            printf("Program exited with code %lu.\n", r->regs[10]);
            return;
          }; break;
          case 169: { // gettimeofday
                      // TODO: actually return the time
            int64_t tv_addr = r->regs[10];
            int64_t tz_addr = r->regs[11];

            uint8_t tv_buf[16] = {
                0xD2, 0x02, 0x96, 0x49,
                0x00, 0x00, 0x00, 0x00, // tv_sec = 1234567890
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, // tv_usec = 0
            };
            memcpy(&r->memory[tv_addr], tv_buf, 16);

            memset(&r->memory[tz_addr], 0, 8);

            r->regs[10] = 0;
          }; break;
          default:
            fprintf(stderr, "Unimplemented syscall: %lu\n", r->regs[17]);
            exit(1);
          }
          break;
        } else {
          fprintf(stderr, "I-type 4: funct3=000 unrecognized imm: %b\n", imm);
          exit(1);
        };
        break;
      } else {
        fprintf(stderr, "I-type 4: unrecognized funct3: %03b\n", funct3);
        exit(1);
      }
    }; break;
    case 0b1101111: { // jal
      PARSE_J_INS(ins);
      r->regs[rd] = r->pc + 4;
      r->pc += imm;
      continue;
    }; break;
    case 0b0110011: {
      PARSE_R_INS(ins);
      if (funct3 == 0b000) {
        if (funct7 == 0b0000000) { // add
          r->regs[rd] = r->regs[rs1] + r->regs[rs2];
        } else if (funct7 == 0b0100000) { // sub
          r->regs[rd] = r->regs[rs1] - r->regs[rs2];
        } else if (funct7 == 0b0000001) { // mul
          r->regs[rd] = r->regs[rs1] * r->regs[rs2];
        } else {
          fprintf(stderr, "R-type 1: funct3=0b000: unrecognized funct7: %b\n",
                  funct7);
          exit(1);
        }
      } else if (funct3 == 0b001) {
        if (funct7 == 0b0000001) { // mulh
          int64_t a_hi = r->regs[rs1] >> 32;
          int64_t b_hi = r->regs[rs2] >> 32;
          uint64_t a_lo = (uint32_t)r->regs[rs1];
          uint64_t b_lo = (uint32_t)r->regs[rs2];
          uint64_t p0 = a_lo * b_lo;
          int64_t p1 = a_hi * b_lo;
          int64_t p2 = b_hi * a_lo;
          int64_t p3 = a_hi * b_hi;
          int64_t carry = (p0 >> 32);
          int64_t mid = p1 + p2 + carry;
          r->regs[rd] = p3 + (mid >> 32);
        } else {
          fprintf(stderr, "R-type 1: funct3=0b001: unrecognized funct7: %b\n",
                  funct7);
          exit(1);
        }
      } else if (funct3 == 0b010) {
        if (funct7 == 0b0000000) { // slt
          r->regs[rd] = ((int64_t)r->regs[rs1] < (int64_t)r->regs[rs2]) ? 1 : 0;
        } else {
          fprintf(stderr, "R-type 1: funct3=0b010: unrecognized funct7: %b\n",
                  funct7);
          exit(1);
        }
      } else if (funct3 == 0b011) {
        if (funct7 == 0b0000000) { // sltu
          r->regs[rd] =
              ((uint64_t)r->regs[rs1] < (uint64_t)r->regs[rs2]) ? 1 : 0;
        } else {
          fprintf(stderr, "R-type 1: funct3=0b011: unrecognized funct7: %b\n",
                  funct7);
          exit(1);
        }
      } else if (funct3 == 0b100) {
        if (funct7 == 0b0000000) { // xor
          r->regs[rd] = r->regs[rs1] ^ r->regs[rs2];
        } else if (funct7 == 0b0000001) { // div
          if (r->regs[rs2] == 0) {
            r->regs[rd] = -1;
          } else {
            r->regs[rd] = (int64_t)r->regs[rs1] / (int64_t)r->regs[rs2];
          }
        } else {
          fprintf(stderr, "R-type 1: funct3=0b100: unrecognized funct7: %b\n",
                  funct7);
          exit(1);
        }
      } else if (funct3 == 0b110) {
        if (funct7 == 0b0000001) { // rem
          if (r->regs[rs2] == 0) {
            r->regs[rd] = r->regs[rs1];
          } else {
            r->regs[rd] = (int64_t)r->regs[rs1] % (int64_t)r->regs[rs2];
          }
        } else if (funct7 == 0b0000000) { // or
          r->regs[rd] = r->regs[rs1] | r->regs[rs2];
        } else {
          fprintf(stderr, "R-type 1: funct3=0b110: unrecognized funct7: %b\n",
                  funct7);
          exit(1);
        }
      } else if (funct3 == 0b111) {
        if (funct7 == 0b000) { // and
          r->regs[rd] = r->regs[rs1] & r->regs[rs2];
        } else {
          fprintf(stderr, "R-type 1: funct3=0b111: unrecognized funct7: %b\n",
                  funct7);
          exit(1);
        }
      } else {
        fprintf(stderr, "R-type 1: unrecognized funct3: %03b\n", funct3);
        exit(1);
      }
    }; break;
    case 0b0111011: {
      PARSE_R_INS(ins);
      if (funct3 == 0b000) {
        if (funct7 == 0b0000000) { // addw
          r->regs[rd] = (int32_t)(r->regs[rs1] + r->regs[rs2]);
        } else if (funct7 == 0b0100000) { // subw
          r->regs[rd] = (int32_t)(r->regs[rs1] - r->regs[rs2]);
        } else if (funct7 == 0b0000001) { // mulw
          r->regs[rd] = (int32_t)(r->regs[rs1] * r->regs[rs2]);
        } else {
          fprintf(stderr, "R-type 3: funct3=000: unrecognized funct7: %b\n",
                  funct7);
          exit(1);
        }
      } else if (funct3 == 0b101) {
        if (funct7 == 0b0000001) { // divuw
          if (r->regs[rs2] == 0) {
            r->regs[rd] = -1LL;
          } else {
            r->regs[rd] =
                (int32_t)((uint32_t)r->regs[rs1] / (uint32_t)r->regs[rs2]);
          }
        } else {
          fprintf(stderr, "R-type 3: funct3=101: unrecognized funct7: %b\n",
                  funct7);
          exit(1);
        }
      } else if (funct3 == 0b111) { // remuw
        if (r->regs[rs2] == 0) {
          r->regs[rd] = (int32_t)r->regs[rs1];
        } else {
          r->regs[rd] =
              (int32_t)((uint32_t)r->regs[rs1] % (uint32_t)r->regs[rs2]);
        }
      } else {
        fprintf(stderr, "R-type 3: unrecognized funct3: %03b\n", funct3);
        exit(1);
      }
    }; break;
    case 0b0011011: {
      PARSE_I_INS(ins);
      if (funct3 == 0b000) { // addiw
        r->regs[rd] = (int32_t)r->regs[rs1] + (int32_t)imm;
      } else if (funct3 == 0b001) {
        uint32_t shamt = (ins >> 20) & 0b11111;
        r->regs[rd] = (int32_t)r->regs[rs1] << shamt;
      } else {
        fprintf(stderr, "R-type 4: unrecognized funct3: %03b\n", funct3);
        exit(1);
      }
    }; break;
    case 0b0100011: {
      PARSE_S_INS(ins);

      if (funct3 == 0b000) { // sb
        size_t addr = r->regs[rs1] + imm;
        r->memory[addr] = r->regs[rs2];
      } else if (funct3 == 0b001) { // sh
        size_t addr = r->regs[rs1] + imm;
        *(uint16_t *)(&r->memory[addr]) = r->regs[rs2];
      } else if (funct3 == 0b010) { // sw
        size_t addr = r->regs[rs1] + imm;
        *(uint32_t *)(&r->memory[addr]) = r->regs[rs2];
      } else if (funct3 == 0b011) { // sd
        size_t addr = r->regs[rs1] + imm;
        // take this rust
        *(uint64_t *)(&r->memory[addr]) = r->regs[rs2];
      } else {
        fprintf(stderr, "S-type: unrecognized funct3: %03b\n", funct3);
        exit(1);
      }
    }; break;
    case 0b0110111: { // lui
      PARSE_U_INS(ins);
      r->regs[rd] = (int64_t)imm << 12;
    }; break;
    case 0b0010111: { // auipc
      PARSE_U_INS(ins);
      r->regs[rd] = r->pc + ((int64_t)imm << 12);
    }; break;
    default:
      fprintf(stderr, "Unrecognized opcode: %07b\n", opcode);
      exit(1);
    }

    r->pc += 4;
  }
}

int main(int argc, char *argv[]) {
  if (elf_version(EV_CURRENT) == EV_NONE) {
    fprintf(stderr, "Failed to initialize libelf: %s\n", elf_errmsg(-1));
    return 1;
  }

  const char *path = NULL;
  for (int i = 1; i < argc; i++) {
    path = argv[i];
  }

  if (path == NULL) {
    fprintf(stderr, "Usage: %s <path>\n", argv[0]);
    return 1;
  }

  FILE *file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "Failed to open %s\n", path);
    return 1;
  }

  // probably enough for anything we can handle
  uint8_t *exe_bytes = malloc(20 * 1024 * 1024);
  size_t exe_size = fread(exe_bytes, 1, 20 * 1024 * 1024, file);
  fclose(file);

  RISCV64 r = riscv64_load(exe_bytes, exe_size);

  for (r.pc = r.code_section.offset;
       r.pc < r.code_section.offset + r.code_section.size; r.pc += 4) {
    riscv64_disassemble_one(&r);
  }
  printf("END DISASSEMBLY\n");

  riscv64_execute(&r);
}
