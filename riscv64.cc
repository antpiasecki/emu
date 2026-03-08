// https://docs.riscv.org/reference/isa/unpriv/rv-32-64g.html
// https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <gelf.h>
#include <iostream>
#include <libelf.h>
#include <vector>

// TODO: dont use macros now that we're in an actual language
#define PARSE_I_INS(ins)                                                       \
  u8 rd = (ins >> 7) & 0b11111;                                                \
  u8 funct3 = (ins >> 12) & 0b111;                                             \
  u8 rs1 = (ins >> 15) & 0b11111;                                              \
  i32 imm = ((i32)ins) >> 20

#define PARSE_R_INS(ins)                                                       \
  u8 rd = (ins >> 7) & 0b11111;                                                \
  u8 funct3 = (ins >> 12) & 0b111;                                             \
  u8 rs1 = (ins >> 15) & 0b11111;                                              \
  u8 rs2 = (ins >> 20) & 0b11111;                                              \
  i32 funct7 = ((i32)ins) >> 25

#define PARSE_B_INS(ins)                                                       \
  u8 funct3 = (ins >> 12) & 0b111;                                             \
  u8 rs1 = (ins >> 15) & 0b11111;                                              \
  u8 rs2 = (ins >> 20) & 0b11111;                                              \
  i32 imm_12 = (ins >> 31) & 0b1;                                              \
  i32 imm_10_5 = (ins >> 25) & 0b111111;                                       \
  i32 imm_4_1 = (ins >> 8) & 0b1111;                                           \
  i32 imm_11 = (ins >> 7) & 0b1;                                               \
  i32 imm =                                                                    \
      (imm_12 << 12) | (imm_11 << 11) | (imm_10_5 << 5) | (imm_4_1 << 1);      \
  imm = (imm << 19) >> 19

#define PARSE_S_INS(ins)                                                       \
  u8 funct3 = (ins >> 12) & 0b111;                                             \
  u8 rs1 = (ins >> 15) & 0b11111;                                              \
  u8 rs2 = (ins >> 20) & 0b11111;                                              \
  i32 imm_11_5 = (ins >> 25) & 0b1111111;                                      \
  i32 imm_4_0 = (ins >> 7) & 0b11111;                                          \
  i32 imm = (imm_11_5 << 5) | imm_4_0;                                         \
  imm = (imm << 20) >> 20

#define PARSE_J_INS(ins)                                                       \
  u8 rd = (ins >> 7) & 0b11111;                                                \
  i32 imm_20 = (ins >> 31) & 0b1;                                              \
  i32 imm_10_1 = (ins >> 21) & 0b1111111111;                                   \
  i32 imm_11 = (ins >> 20) & 0b1;                                              \
  i32 imm_19_12 = (ins >> 12) & 0b11111111;                                    \
  i32 imm =                                                                    \
      (imm_20 << 20) | (imm_19_12 << 12) | (imm_11 << 11) | (imm_10_1 << 1);   \
  imm = (imm << 11) >> 11

#define PARSE_U_INS(ins)                                                       \
  u8 rd = (ins >> 7) & 0b11111;                                                \
  i32 imm = ins >> 12

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
static_assert(sizeof(size_t) == sizeof(u64), "u64 must be 64-bit");
static_assert(std::endian::native == std::endian::little,
              "Big endianness not supported");

static constexpr u64 MEMORY_SIZE = 20 * 1024 * 1024; // should be enough

static constexpr std::array<const char *, 32> REGS = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "fp", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

struct Section {
  u64 offset;
  u64 size;
  u64 entrypoint;
};

class RISCV64 {
public:
  RISCV64(const std::vector<char> &exe_bytes) {
    Elf *elf =
        elf_memory(const_cast<char *>(exe_bytes.data()), exe_bytes.size());
    GElf_Ehdr ehdr;
    gelf_getehdr(elf, &ehdr);

    m_code_section = get_code_section(elf, ehdr);
    m_pc = m_code_section.offset;

    for (u64 i = 0; i < ehdr.e_phnum; i++) {
      GElf_Phdr phdr;
      gelf_getphdr(elf, i, &phdr);
      if (phdr.p_type == PT_LOAD) {
        std::copy_n(exe_bytes.data() + phdr.p_offset, phdr.p_filesz,
                    m_memory.data() + phdr.p_vaddr);
      }
    }
    elf_end(elf);
  }

  void disassemble_all() {
    for (m_pc = m_code_section.offset;
         m_pc < m_code_section.offset + m_code_section.size; m_pc += 4) {
      disassemble_one();
    }
  }

  void disassemble_one() {
    u32 ins;
    std::memcpy(&ins, m_memory.data() + m_pc, sizeof(ins));

    u16 opcode = ins & 0b1111111;

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
      } else if (funct3 == 0b111) {
        printf("bgeu %s, %s, %d\n", REGS[rs1], REGS[rs2], imm);
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
        u32 shamt = (ins >> 20) & 0b111111;
        u32 funct6 = (ins >> 26) & 0b111111;

        if (funct6 == 0b000000) {
          printf("slli %s, %s, %d\n", REGS[rd], REGS[rs1], shamt);
        } else {
          fprintf(stderr, "I-type 1: funct3=001: unrecognized funct6: %b\n",
                  funct6);
          exit(1);
        }
      } else if (funct3 == 0b010) {
        printf("slti %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
      } else if (funct3 == 0b011) {
        printf("sltiu %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
      } else if (funct3 == 0b100) {
        printf("xori %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
      } else if (funct3 == 0b101) {
        // of course this one just has to be different
        u32 shamt = (ins >> 20) & 0b111111;
        u32 funct6 = (ins >> 26) & 0b111111;

        if (funct6 == 0b000000) {
          printf("srli %s, %s, %d\n", REGS[rd], REGS[rs1], shamt);
        } else if (funct6 == 0b010000) {
          printf("srai %s, %s, %d\n", REGS[rd], REGS[rs1], shamt);
        } else {
          fprintf(stderr, "I-type 1: funct3=101: unrecognized funct6: %b\n",
                  funct6);
          exit(1);
        }
      } else if (funct3 == 0b110) {
        printf("ori %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
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
      } else if (funct3 == 0b101) {
        printf("lhu %s, %d(%s)\n", REGS[rd], imm, REGS[rs1]);
      } else if (funct3 == 0b110) {
        printf("lwu %s, %d(%s)\n", REGS[rd], imm, REGS[rs1]);
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
        if (funct7 == 0b0000000) {
          printf("sll %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
        } else if (funct7 == 0b0000001) {
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
      } else if (funct3 == 0b101) {
        if (funct7 == 0b0000000) {
          printf("srl %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
        } else if (funct7 == 0b0000001) {
          printf("divu %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
        } else if (funct7 == 0b0100000) {
          printf("sra %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
        } else {
          fprintf(stderr, "R-type 1: funct3=0b101: unrecognized funct7: %b\n",
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
        } else if (funct7 == 0b001) {
          printf("remu %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
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
      fprintf(stderr, "A extension not implemented yet.\n");
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
      } else if (funct3 == 0b001) {
        printf("sllw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else if (funct3 == 0b100) {
        printf("divw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else if (funct3 == 0b101) {
        if (funct7 == 0b0000000) {
          printf("srlw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
        } else if (funct7 == 0b0100000) {
          printf("sraw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
        } else if (funct7 == 0b0000001) {
          printf("divuw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
        } else {
          fprintf(stderr, "R-type 3: funct3=101: unrecognized funct7: %b\n",
                  funct7);
          exit(1);
        }
      } else if (funct3 == 0b110) {
        printf("remw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else if (funct3 == 0b111) {
        printf("remuw %s, %s, %s\n", REGS[rd], REGS[rs1], REGS[rs2]);
      } else {
        fprintf(stderr, "R-type 3: unrecognized funct3: %03b\n", funct3);
        exit(1);
      }
    }; break;
    case 0b0011011: {
      u8 funct3 = (ins >> 12) & 0b111;
      if (funct3 == 0b000) {
        PARSE_I_INS(ins);
        printf("addiw %s, %s, %d\n", REGS[rd], REGS[rs1], imm);
      } else if (funct3 == 0b001) {
        PARSE_R_INS(ins);
        printf("slliw %s, %s, %d\n", REGS[rd], REGS[rs1], rs2);
      } else if (funct3 == 0b101) {
        PARSE_R_INS(ins);
        if (funct7 == 0b0000000) {
          printf("srliw %s, %s, %d\n", REGS[rd], REGS[rs1], rs2);
        } else if (funct7 == 0b0100000) {
          printf("sraiw %s, %s, %d\n", REGS[rd], REGS[rs1], rs2);
        } else {
          fprintf(stderr, "R-type 4: funct3=101: unrecognized funct7: %b\n",
                  funct7);
          exit(1);
        }
      } else {
        fprintf(stderr, "R-type 4: unrecognized funct3: %03b\n", funct3);
        exit(1);
      }
    }; break;
    case 0b0100111: {
      fprintf(stderr, "F extension not implemented yet.\n");
      exit(1);
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

  void dump() {
    std::cout << "REGS:";
    for (u64 i = 0; i < 32; i++) {
      std::cout << " " << m_regs[i];
    }
    std::cout << "\n";
  }

  void execute() {
    m_pc = m_code_section.entrypoint;

    m_regs[2] = MEMORY_SIZE - 1024; // set the stack pointer

    while (m_pc < m_code_section.offset + m_code_section.size) {
      m_regs[0] = 0; // clear the zero register

      u32 ins;
      std::memcpy(&ins, m_memory.data() + m_pc, sizeof(ins));

      u16 opcode = ins & 0b1111111;

      // https://stackoverflow.com/questions/62939410/how-can-i-find-out-the-instruction-format-of-a-risc-v-instruction
      switch (opcode) {
      case 0b1100011: {
        PARSE_B_INS(ins);

        if (funct3 == 0b000) { // beq
          if (m_regs[rs1] == m_regs[rs2]) {
            m_pc += imm;
            continue;
          }
        } else if (funct3 == 0b001) { // bne
          if (m_regs[rs1] != m_regs[rs2]) {
            m_pc += imm;
            continue;
          }
        } else if (funct3 == 0b101) { // bge
          if (m_regs[rs1] >= m_regs[rs2]) {
            m_pc += imm;
            continue;
          }
        } else if (funct3 == 0b100) { // blt
          if (m_regs[rs1] < m_regs[rs2]) {
            m_pc += imm;
            continue;
          }
        } else if (funct3 == 0b110) { // bltu
          if ((u64)m_regs[rs1] < (u64)m_regs[rs2]) {
            m_pc += imm;
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
          m_regs[rd] = m_regs[rs1] + imm;
        } else if (funct3 == 0b001) { // slli
          u32 shamt = (ins >> 20) & 0b111111;
          u32 funct6 = (ins >> 26) & 0b111111;

          if (funct6 == 0b000000) {
            m_regs[rd] = m_regs[rs1] << shamt;
          } else {
            fprintf(stderr, "I-type 1: funct3=001: unrecognized funct6: %b\n",
                    funct6);
            exit(1);
          }
        } else if (funct3 == 0b011) { // sltiu
          m_regs[rd] = ((u64)m_regs[rs1] < (u64)(i64)imm) ? 1 : 0;
        } else if (funct3 == 0b100) { // xori
          m_regs[rd] = m_regs[rs1] ^ imm;
        } else if (funct3 == 0b101) {
          // of course this one just has to be different
          u32 shamt = (ins >> 20) & 0b111111;
          u32 funct6 = (ins >> 26) & 0b111111;

          if (funct6 == 0b000000) { // srli
            m_regs[rd] = (u64)m_regs[rs1] >> shamt;
          } else if (funct6 == 0b010000) { // srai
            m_regs[rd] = (i64)m_regs[rs1] >> shamt;
          } else {
            fprintf(stderr, "I-type 1: funct3=101: unrecognized funct6: %b\n",
                    funct6);
            exit(1);
          }
        } else if (funct3 == 0b111) {
          m_regs[rd] = m_regs[rs1] & imm;
        } else {
          fprintf(stderr, "I-type 1: unrecognized funct3: %03b\n", funct3);
          exit(1);
        }
      }; break;
      case 0b0000011: {
        PARSE_I_INS(ins);

        if (funct3 == 0b000) { // lb
          m_regs[rd] = (i8)m_memory[m_regs[rs1] + imm];
        } else if (funct3 == 0b001) { // lh
          m_regs[rd] = *(i16 *)&m_memory[m_regs[rs1] + imm];
        } else if (funct3 == 0b010) { // lw
          m_regs[rd] = *(i32 *)&m_memory[m_regs[rs1] + imm];
        } else if (funct3 == 0b011) { // ld
          m_regs[rd] = *(u64 *)&m_memory[m_regs[rs1] + imm];
        } else if (funct3 == 0b100) { // lbu
          m_regs[rd] = m_memory[m_regs[rs1] + imm];
        } else {
          fprintf(stderr, "I-type 2: unrecognized funct3: %03b\n", funct3);
          exit(1);
        }
      }; break;
      case 0b1100111: { // jalr
        PARSE_I_INS(ins);
        u64 target = (m_regs[rs1] + imm) & ~(u64)1;
        m_regs[rd] = m_pc + 4;
        m_pc = target;
        continue;
      }; break;
      case 0b1110011: {
        PARSE_I_INS(ins);

        if (funct3 == 0b000) {
          if (imm == 0) { // ecall
            // https://jborza.com/post/2021-05-11-riscv-linux-syscalls/
            switch (m_regs[17]) {
            case 63: { // read
              if (m_regs[10] != 0) {
                fprintf(stderr, "read syscall implemented only for stdin.\n");
                exit(1);
              }

              u64 start = m_regs[11];
              u64 count = m_regs[12];
              u64 bytes_read = 0;

              for (u64 i = 0; i < count; i++) {
                int c = getchar();
                if (c == EOF)
                  break;
                m_memory[start + i] = (u8)c;
                bytes_read++;
                if (c == '\n')
                  break;
              }

              m_regs[10] = bytes_read;
            }; break;
            case 64: { // write
              if (m_regs[10] != 1) {
                fprintf(stderr, "write syscall implemented only for stdout.\n");
                exit(1);
              }

              u64 start = m_regs[11];
              u64 end = m_regs[11] + m_regs[12];

              for (u64 i = start; i < end; i++) {
                putchar(m_memory[i]);
              }
            }; break;
            case 93: { // exit
              printf("Program exited with code %lu.\n", m_regs[10]);
              return;
            }; break;
            case 169: { // gettimeofday
                        // TODO: actually return the time
              i64 tv_addr = m_regs[10];
              i64 tz_addr = m_regs[11];

              u8 tv_buf[16] = {
                  0xD2, 0x02, 0x96, 0x49,
                  0x00, 0x00, 0x00, 0x00, // tv_sec = 1234567890
                  0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, // tv_usec = 0
              };
              memcpy(&m_memory[tv_addr], tv_buf, 16);

              memset(&m_memory[tz_addr], 0, 8);

              m_regs[10] = 0;
            }; break;
            default:
              fprintf(stderr, "Unimplemented syscall: %lu\n", m_regs[17]);
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
        m_regs[rd] = m_pc + 4;
        m_pc += imm;
        continue;
      }; break;
      case 0b0110011: {
        PARSE_R_INS(ins);
        if (funct3 == 0b000) {
          if (funct7 == 0b0000000) { // add
            m_regs[rd] = m_regs[rs1] + m_regs[rs2];
          } else if (funct7 == 0b0100000) { // sub
            m_regs[rd] = m_regs[rs1] - m_regs[rs2];
          } else if (funct7 == 0b0000001) { // mul
            m_regs[rd] = m_regs[rs1] * m_regs[rs2];
          } else {
            fprintf(stderr, "R-type 1: funct3=0b000: unrecognized funct7: %b\n",
                    funct7);
            exit(1);
          }
        } else if (funct3 == 0b001) {
          if (funct7 == 0b0000001) { // mulh
            i64 a_hi = m_regs[rs1] >> 32;
            i64 b_hi = m_regs[rs2] >> 32;
            u64 a_lo = (u32)m_regs[rs1];
            u64 b_lo = (u32)m_regs[rs2];
            u64 p0 = a_lo * b_lo;
            i64 p1 = a_hi * b_lo;
            i64 p2 = b_hi * a_lo;
            i64 p3 = a_hi * b_hi;
            i64 carry = (p0 >> 32);
            i64 mid = p1 + p2 + carry;
            m_regs[rd] = p3 + (mid >> 32);
          } else {
            fprintf(stderr, "R-type 1: funct3=0b001: unrecognized funct7: %b\n",
                    funct7);
            exit(1);
          }
        } else if (funct3 == 0b010) {
          if (funct7 == 0b0000000) { // slt
            m_regs[rd] = (m_regs[rs1] < m_regs[rs2]) ? 1 : 0;
          } else {
            fprintf(stderr, "R-type 1: funct3=0b010: unrecognized funct7: %b\n",
                    funct7);
            exit(1);
          }
        } else if (funct3 == 0b011) {
          if (funct7 == 0b0000000) { // sltu
            m_regs[rd] = ((u64)m_regs[rs1] < (u64)m_regs[rs2]) ? 1 : 0;
          } else if (funct7 == 0b0000001) { // mulhu
            u64 a = m_regs[rs1];
            u64 b = m_regs[rs2];
            u64 a_lo = a & 0xffffffff;
            u64 a_hi = a >> 32;
            u64 b_lo = b & 0xffffffff;
            u64 b_hi = b >> 32;
            u64 lo_lo = a_lo * b_lo;
            u64 hi_lo = a_hi * b_lo;
            u64 lo_hi = a_lo * b_hi;
            u64 hi_hi = a_hi * b_hi;
            u64 mid =
                (lo_lo >> 32) + (hi_lo & 0xffffffff) + (lo_hi & 0xffffffff);
            m_regs[rd] = hi_hi + (hi_lo >> 32) + (lo_hi >> 32) + (mid >> 32);
          } else {
            fprintf(stderr, "R-type 1: funct3=0b011: unrecognized funct7: %b\n",
                    funct7);
            exit(1);
          }
        } else if (funct3 == 0b100) {
          if (funct7 == 0b0000000) { // xor
            m_regs[rd] = m_regs[rs1] ^ m_regs[rs2];
          } else if (funct7 == 0b0000001) { // div
            if (m_regs[rs2] == 0) {
              m_regs[rd] = -1;
            } else {
              m_regs[rd] = m_regs[rs1] / m_regs[rs2];
            }
          } else {
            fprintf(stderr, "R-type 1: funct3=0b100: unrecognized funct7: %b\n",
                    funct7);
            exit(1);
          }
        } else if (funct3 == 0b110) {
          if (funct7 == 0b0000001) { // rem
            if (m_regs[rs2] == 0) {
              m_regs[rd] = m_regs[rs1];
            } else {
              m_regs[rd] = m_regs[rs1] % m_regs[rs2];
            }
          } else if (funct7 == 0b0000000) { // or
            m_regs[rd] = m_regs[rs1] | m_regs[rs2];
          } else {
            fprintf(stderr, "R-type 1: funct3=0b110: unrecognized funct7: %b\n",
                    funct7);
            exit(1);
          }
        } else if (funct3 == 0b111) {
          if (funct7 == 0b000) { // and
            m_regs[rd] = m_regs[rs1] & m_regs[rs2];
          } else if (funct7 == 0b001) { // remu
            if (m_regs[rs2] == 0) {
              m_regs[rd] = m_regs[rs1];
            } else {
              m_regs[rd] = (u64)m_regs[rs1] % (u64)m_regs[rs2];
            }
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
      case 0b0101111: {
        fprintf(stderr, "A extension not implemented yet.\n");
        exit(1);
      }; break;
      case 0b0111011: {
        PARSE_R_INS(ins);
        if (funct3 == 0b000) {
          if (funct7 == 0b0000000) { // addw
            m_regs[rd] = (i32)(m_regs[rs1] + m_regs[rs2]);
          } else if (funct7 == 0b0100000) { // subw
            m_regs[rd] = (i32)(m_regs[rs1] - m_regs[rs2]);
          } else if (funct7 == 0b0000001) { // mulw
            m_regs[rd] = (i32)(m_regs[rs1] * m_regs[rs2]);
          } else {
            fprintf(stderr, "R-type 3: funct3=000: unrecognized funct7: %b\n",
                    funct7);
            exit(1);
          }
        } else if (funct3 == 0b001) {
          if (funct7 == 0b0000000) { // sllw
            m_regs[rd] =
                (i32)(u32)((u32)m_regs[rs1] << ((u32)m_regs[rs2] & 0b11111));
          } else {
            fprintf(stderr, "R-type 3: funct3=001: unrecognized funct7: %b\n",
                    funct7);
            exit(1);
          }
        } else if (funct3 == 0b100) { // divw
          i32 a = m_regs[rs1];
          i32 b = m_regs[rs2];
          if (b == 0) {
            m_regs[rd] = (u64)(i64)(-1);
          } else {
            m_regs[rd] = (i64)(a / b);
          }
        } else if (funct3 == 0b101) {
          if (funct7 == 0b0000000) { // srlw
            m_regs[rd] =
                (i32)((u32)m_regs[rs1] >> ((u32)m_regs[rs2] & 0b11111));
          } else if (funct7 == 0b0100000) { // sraw
            m_regs[rd] = ((i32)m_regs[rs1]) >> ((u32)m_regs[rs2] & 0b11111);
          } else if (funct7 == 0b0000001) { // divuw
            if (m_regs[rs2] == 0) {
              m_regs[rd] = -1LL;
            } else {
              m_regs[rd] = (i32)((u32)m_regs[rs1] / (u32)m_regs[rs2]);
            }
          } else {
            fprintf(stderr, "R-type 3: funct3=101: unrecognized funct7: %b\n",
                    funct7);
            exit(1);
          }
        } else if (funct3 == 0b111) { // remuw
          if (m_regs[rs2] == 0) {
            m_regs[rd] = (i32)m_regs[rs1];
          } else {
            m_regs[rd] = (i32)((u32)m_regs[rs1] % (u32)m_regs[rs2]);
          }
        } else {
          fprintf(stderr, "R-type 3: unrecognized funct3: %03b\n", funct3);
          exit(1);
        }
      }; break;
      case 0b0011011: {
        PARSE_I_INS(ins);
        if (funct3 == 0b000) { // addiw
          m_regs[rd] = (i32)m_regs[rs1] + (i32)imm;
        } else if (funct3 == 0b001) {
          u32 shamt = (ins >> 20) & 0b11111;
          m_regs[rd] = (i32)m_regs[rs1] << shamt;
        } else if (funct3 == 0b101) {
          PARSE_R_INS(ins);
          if (funct7 == 0b0000000) { // srliw
            m_regs[rd] = (i32)((u32)m_regs[rs1] >> rs2);
          } else if (funct7 == 0b0100000) { // sraiw
            m_regs[rd] = (i32)m_regs[rs1] >> rs2;
          } else {
            fprintf(stderr, "R-type 4: funct3=101: unrecognized funct7: %b\n",
                    funct7);
            exit(1);
          }
        } else {
          fprintf(stderr, "R-type 4: unrecognized funct3: %03b\n", funct3);
          exit(1);
        }
      }; break;
      case 0b0100011: {
        PARSE_S_INS(ins);

        if (funct3 == 0b000) { // sb
          u64 addr = m_regs[rs1] + imm;
          m_memory[addr] = m_regs[rs2];
        } else if (funct3 == 0b001) { // sh
          u64 addr = m_regs[rs1] + imm;
          *(u16 *)(&m_memory[addr]) = m_regs[rs2];
        } else if (funct3 == 0b010) { // sw
          u64 addr = m_regs[rs1] + imm;
          *(u32 *)(&m_memory[addr]) = m_regs[rs2];
        } else if (funct3 == 0b011) { // sd
          u64 addr = m_regs[rs1] + imm;
          // take this rust
          *(u64 *)(&m_memory[addr]) = m_regs[rs2];
        } else {
          fprintf(stderr, "S-type: unrecognized funct3: %03b\n", funct3);
          exit(1);
        }
      }; break;
      case 0b0110111: { // lui
        PARSE_U_INS(ins);
        m_regs[rd] = (i64)(i32)(imm << 12);
      }; break;
      case 0b0010111: { // auipc
        PARSE_U_INS(ins);
        m_regs[rd] = m_pc + ((i64)imm << 12);
      }; break;
      default:
        fprintf(stderr, "Unrecognized opcode: %07b\n", opcode);
        exit(1);
      }

      m_pc += 4;
    }
  }

private:
  std::vector<u8> m_memory = std::vector<u8>(MEMORY_SIZE, 0);
  u64 m_pc;
  std::array<i64, 32> m_regs{};
  Section m_code_section;

  static Section get_code_section(Elf *elf, GElf_Ehdr ehdr) {
    u64 str_table_index;
    if (elf_getshdrstrndx(elf, &str_table_index) != 0) {
      std::cerr << "elf_getshdrstrndx failed: " << elf_errmsg(-1) << "\n";
      exit(1);
    }

    Elf_Scn *section = nullptr;
    while ((section = elf_nextscn(elf, section)) != nullptr) {
      GElf_Shdr header;
      if (gelf_getshdr(section, &header) != &header)
        continue;

      const char *name = elf_strptr(elf, str_table_index, header.sh_name);
      if (name && std::string_view(name) == ".text") {
        return Section{.offset = header.sh_addr,
                       .size = header.sh_size,
                       .entrypoint = ehdr.e_entry};
      }
    }

    std::cerr << "Failed to locate .text\n";
    exit(1);
  }
};

int main(int argc, char *argv[]) {
  if (elf_version(EV_CURRENT) == EV_NONE) {
    std::cerr << "Failed to initialize libelf: " << elf_errmsg(-1) << "\n";
    return 1;
  }

  const char *path = nullptr;
  for (int i = 1; i < argc; i++) {
    path = argv[i];
  }

  if (path == nullptr) {
    std::cerr << "Usage: " << argv[0] << " <path>\n";
    return 1;
  }

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "Failed to open: " << path << "\n";
    return 1;
  }

  i64 exe_size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> exe_bytes(exe_size);
  file.read(exe_bytes.data(), exe_size);
  file.close();

  RISCV64 r(exe_bytes);

  exe_bytes.clear();
  exe_bytes.shrink_to_fit();

  r.disassemble_all();
  printf("END DISASSEMBLY\n");

  r.execute();
}
