#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARSE_I_INS(ins)                                                       \
  uint8_t rd = (ins >> 7) & 0b11111;                                           \
  uint8_t funct3 = (ins >> 12) & 0b111;                                        \
  uint8_t rs1 = (ins >> 15) & 0b11111;                                         \
  int32_t imm = ((int32_t)ins) >> 20

#define PARSE_R_INS(ins)                                                       \
  uint8_t rd = (ins >> 7) & 0b11111;                                           \
  uint8_t funct3 = (ins >> 12) & 0b111;                                        \
  uint8_t rs1 = (ins >> 15) & 0b11111;                                         \
  uint8_t rs2 = (ins >> 20) & 0b11111;                                         \
  int32_t funct7 = ((int32_t)ins) >> 25

#define PARSE_B_INS(ins)                                                       \
  uint8_t funct3 = (ins >> 12) & 0b111;                                        \
  uint8_t rs1 = (ins >> 15) & 0b11111;                                         \
  uint8_t rs2 = (ins >> 20) & 0b11111;                                         \
  int32_t imm_12 = (ins >> 31) & 0b1;                                          \
  int32_t imm_10_5 = (ins >> 25) & 0b111111;                                   \
  int32_t imm_4_1 = (ins >> 8) & 0b1111;                                       \
  int32_t imm_11 = (ins >> 7) & 0b1;                                           \
  int32_t imm =                                                                \
      (imm_12 << 12) | (imm_11 << 11) | (imm_10_5 << 5) | (imm_4_1 << 1);      \
  imm = (imm << 19) >> 19

#define PARSE_S_INS(ins)                                                       \
  uint8_t funct3 = (ins >> 12) & 0b111;                                        \
  uint8_t rs1 = (ins >> 15) & 0b11111;                                         \
  uint8_t rs2 = (ins >> 20) & 0b11111;                                         \
  int32_t imm_11_5 = (ins >> 25) & 0b1111111;                                  \
  int32_t imm_4_0 = (ins >> 7) & 0b11111;                                      \
  int32_t imm = (imm_11_5 << 5) | imm_4_0;                                     \
  imm = (imm << 20) >> 20

#define PARSE_J_INS(ins)                                                       \
  uint8_t rd = (ins >> 7) & 0b11111;                                           \
  int32_t imm_20 = (ins >> 31) & 0b1;                                          \
  int32_t imm_10_1 = (ins >> 21) & 0b1111111111;                               \
  int32_t imm_11 = (ins >> 20) & 0b1;                                          \
  int32_t imm_19_12 = (ins >> 12) & 0b11111111;                                \
  int32_t imm =                                                                \
      (imm_20 << 20) | (imm_19_12 << 12) | (imm_11 << 11) | (imm_10_1 << 1);   \
  imm = (imm << 11) >> 11

#define PARSE_U_INS(ins)                                                       \
  uint8_t rd = (ins >> 7) & 0b11111;                                           \
  int32_t imm = ins >> 12

typedef struct {
  size_t offset;
  size_t size;
  size_t entrypoint;
} Section;

typedef struct {
  uint8_t *memory;
  size_t pc;
  int64_t regs[32];
  Section code_section;
} RISCV64;

extern const char *const REGS[32];

inline void riscv64_disassemble_one(const RISCV64 *r) {
  uint32_t ins;
  memcpy(&ins, r->memory + r->pc, 4);

  uint16_t opcode = ins & 0b1111111;

  // https://stackoverflow.com/questions/62939410/how-can-i-find-out-the-instruction-format-of-a-risc-v-instruction
  switch (opcode) {
  case 0b1100011: {
    PARSE_B_INS(ins);

    if (funct3 == 0b000) {
      printf("beq %s, %s, %d\n", REGS[rs1], REGS[rs2], imm);
    } else if (funct3 == 0b001) {
      printf("bne %s, %s, %d\n", REGS[rs1], REGS[rs2], imm);
    } else if (funct3 == 0b100) {
      printf("blt %s, %s, %d\n", REGS[rs1], REGS[rs2], imm);
    } else if (funct3 == 0b101) {
      printf("bge %s, %s, %d\n", REGS[rs1], REGS[rs2], imm);
    } else if (funct3 == 0b110) {
      printf("bltu %s, %s, %d\n", REGS[rs1], REGS[rs2], imm);
    } else {
      fprintf(stderr, "B-type: unrecognized funct3: %03b\n", funct3);
      exit(1);
    }
  }; break;
  case 0b0010011: {
    PARSE_I_INS(ins);

    if (funct3 == 0b000) {
      printf("addi %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
    } else if (funct3 == 0b001) {
      uint32_t shamt = (ins >> 20) & 0b111111;
      uint32_t funct6 = (ins >> 26) & 0b111111;

      if (funct6 == 0b000000) {
        printf("slli %s, %s, %d\n", REGS[rd], REGS[rs1], shamt);
      } else {
        fprintf(stderr, "I-type 1: funct3=001: unrecognized funct6: %b\n",
                funct6);
        exit(1);
      }
    } else if (funct3 == 0b011) {
      printf("sltiu %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
    } else if (funct3 == 0b100) {
      printf("xori %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
    } else if (funct3 == 0b101) {
      // of course this one just has to be different
      uint32_t shamt = (ins >> 20) & 0b111111;
      uint32_t funct6 = (ins >> 26) & 0b111111;

      if (funct6 == 0b000000) {
        printf("srli %s, %s, %d\n", REGS[rd], REGS[rs1], shamt);
      } else if (funct6 == 0b010000) {
        printf("srai %s, %s, %d\n", REGS[rd], REGS[rs1], shamt);
      } else {
        fprintf(stderr, "I-type 1: funct3=101: unrecognized funct6: %b\n",
                funct6);
        exit(1);
      }
    } else if (funct3 == 0b111) {
      printf("andi %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
    } else {
      fprintf(stderr, "I-type 1: unrecognized funct3: %03b\n", funct3);
      exit(1);
    }
  }; break;
  case 0b0000011: {
    PARSE_I_INS(ins);

    if (funct3 == 0b000) {
      printf("lb %s, %d(%s)\n", REGS[rd], imm, REGS[rs1]);
    } else if (funct3 == 0b001) {
      printf("lh %s, %d(%s)\n", REGS[rd], imm, REGS[rs1]);
    } else if (funct3 == 0b010) {
      printf("lw %s, %d(%s)\n", REGS[rd], imm, REGS[rs1]);
    } else if (funct3 == 0b011) {
      printf("ld %s, %d(%s)\n", REGS[rd], imm, REGS[rs1]);
    } else if (funct3 == 0b100) {
      printf("lbu %s, %d(%s)\n", REGS[rd], imm, REGS[rs1]);
    } else {
      fprintf(stderr, "I-type 2: unrecognized funct3: %03b\n", funct3);
      exit(1);
    }
  }; break;
  case 0b1100111: {
    PARSE_I_INS(ins);
    printf("jalr %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
  }; break;
  case 0b1110011: {
    PARSE_I_INS(ins);

    if (funct3 == 0b000) {
      if (imm == 0) {
        printf("ecall\n");
      } else {
        fprintf(stderr, "I-type 4: funct3=000 unrecognized imm: %b\n", imm);
        exit(1);
      }
    } else {
      fprintf(stderr, "I-type 4: unrecognized funct3: %03b\n", funct3);
      exit(1);
    }
  }; break;
  case 0b1101111: {
    PARSE_J_INS(ins);
    printf("jal %s, %d\n", REGS[rd], imm);
  }; break;
  case 0b0110011: {
    PARSE_R_INS(ins);
    if (funct3 == 0b000) {
      if (funct7 == 0b0000000) {
        printf("add %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else if (funct7 == 0b0100000) {
        printf("sub %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else if (funct7 == 0b0000001) {
        printf("mul %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else {
        fprintf(stderr, "R-type 1: funct3=0b000: unrecognized funct7: %b\n",
                funct7);
        exit(1);
      }
    } else if (funct3 == 0b001) {
      if (funct7 == 0b0000001) {
        printf("mulh %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else {
        fprintf(stderr, "R-type 1: funct3=0b001: unrecognized funct7: %b\n",
                funct7);
        exit(1);
      }
    } else if (funct3 == 0b010) {
      if (funct7 == 0b0000000) {
        printf("slt %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else {
        fprintf(stderr, "R-type 1: funct3=0b010: unrecognized funct7: %b\n",
                funct7);
        exit(1);
      }
    } else if (funct3 == 0b011) {
      if (funct7 == 0b0000000) {
        printf("sltu %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else if (funct7 == 0b0000001) {
        printf("mulhu %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else {
        fprintf(stderr, "R-type 1: funct3=0b011: unrecognized funct7: %b\n",
                funct7);
        exit(1);
      }
    } else if (funct3 == 0b100) {
      if (funct7 == 0b0000000) {
        printf("xor %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else if (funct7 == 0b0000001) {
        printf("div %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else {
        fprintf(stderr, "R-type 1: funct3=0b100: unrecognized funct7: %b\n",
                funct7);
        exit(1);
      }
    } else if (funct3 == 0b110) {
      if (funct7 == 0b0000001) {
        printf("rem %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else if (funct7 == 0b0000000) {
        printf("or %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else {
        fprintf(stderr, "R-type 1: funct3=0b110: unrecognized funct7: %b\n",
                funct7);
        exit(1);
      }
    } else if (funct3 == 0b111) {
      if (funct7 == 0b000) {
        printf("and %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
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
  case 0b0101111:
    fprintf(stderr, "R-type 2: unimplemented\n");
    exit(1);
  case 0b0111011: {
    PARSE_R_INS(ins);
    if (funct3 == 0b000) {
      if (funct7 == 0b0000000) {
        printf("addw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else if (funct7 == 0b0100000) {
        printf("subw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else if (funct7 == 0b0000001) {
        printf("mulw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else {
        fprintf(stderr, "R-type 3: funct3=000: unrecognized funct7: %b\n",
                funct7);
        exit(1);
      }
    } else if (funct3 == 0b101) {
      if (funct7 == 0b0000001) {
        printf("divuw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else {
        fprintf(stderr, "R-type 3: funct3=101: unrecognized funct7: %b\n",
                funct7);
        exit(1);
      }
    } else if (funct3 == 0b111) {
      printf("remuw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
    } else {
      fprintf(stderr, "R-type 3: unrecognized funct3: %03b\n", funct3);
      exit(1);
    }
  }; break;
  case 0b0011011: {
    uint8_t funct3 = (ins >> 12) & 0b111;
    if (funct3 == 0b000) {
      PARSE_I_INS(ins);
      printf("addiw %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
    } else if (funct3 == 0b001) {
      PARSE_I_INS(ins);
      printf("slliw %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
    } else {
      fprintf(stderr, "R-type 4: unrecognized funct3: %03b\n", funct3);
      exit(1);
    }
  }; break;
  case 0b0100011: {
    PARSE_S_INS(ins);

    if (funct3 == 0b000) {
      printf("sb %s, %d(%s)\n", REGS[rs2], imm, REGS[rs1]);
    } else if (funct3 == 0b001) {
      printf("sh %s, %d(%s)\n", REGS[rs2], imm, REGS[rs1]);
    } else if (funct3 == 0b010) {
      printf("sw %s, %d(%s)\n", REGS[rs2], imm, REGS[rs1]);
    } else if (funct3 == 0b011) {
      printf("sd %s, %d(%s)\n", REGS[rs2], imm, REGS[rs1]);
    } else {
      fprintf(stderr, "S-type: unrecognized funct3: %03b\n", funct3);
      exit(1);
    }
  }; break;
  case 0b0110111: {
    PARSE_U_INS(ins);
    printf("lui %s, %d\n", REGS[rd], imm);
  }; break;
  case 0b0010111: {
    PARSE_U_INS(ins);
    printf("auipc %s, %d\n", REGS[rd], imm);
  }; break;
  default:
    fprintf(stderr, "Unrecognized opcode: %07b\n", opcode);
    exit(1);
  }
}
