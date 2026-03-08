// https://docs.riscv.org/reference/isa/unpriv/rv-32-64g.html
// https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf

#if !defined(__cplusplus) || __cplusplus < 202302L
#error "C++23 or later is required. Either get a newer compiler " \
       "or manually edit this file to include fmtlib (https://github.com/fmtlib/fmt) " \
       "instead of <print> and remove this check."
#endif

#include <cstring>
#include <fstream>
#include <gelf.h>
#include <iostream>
#include <libelf.h>
#include <print>
#include <vector>

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

enum Op {
  INVALID,

  ADD,
  ADDI,
  ADDIW,
  ADDW,
  AND,
  ANDI,
  AUIPC,
  BEQ,
  BGE,
  BGEU,
  BLT,
  BLTU,
  BNE,
  DIV,
  DIVU,
  DIVUW,
  DIVW,
  ECALL,
  JAL,
  JALR,
  LB,
  LBU,
  LD,
  LH,
  LHU,
  LUI,
  LW,
  LWU,
  MUL,
  MULH,
  MULHU,
  MULW,
  OR,
  ORI,
  REM,
  REMU,
  REMUW,
  REMW,
  SB,
  SD,
  SH,
  SLL,
  SLLI,
  SLLIW,
  SLLW,
  SLT,
  SLTI,
  SLTIU,
  SLTU,
  SRA,
  SRAI,
  SRAIW,
  SRAW,
  SRL,
  SRLI,
  SRLIW,
  SRLW,
  SUB,
  SUBW,
  SW,
  XOR,
  XORI
};

struct Ins {
  Op op;
  u8 rd;
  u8 funct3;
  u8 rs1;
  u8 rs2;
  u8 funct7;
  u8 shamt;
  u8 funct6;
  i32 imm;
};

struct Section {
  u64 offset;
  u64 size;
  u64 entrypoint;
};

enum class Format { NONE, R, I, I_LOAD, I_SHIFT, S, B, U, J };

struct OpDef {
  const char *mnemonic;
  Format format;
};

static constexpr std::array<OpDef, 63> OP_TABLE = {{
    {"???", Format::NONE},      {"add", Format::R},
    {"addi", Format::I},        {"addiw", Format::I},
    {"addw", Format::R},        {"and", Format::R},
    {"andi", Format::I},        {"auipc", Format::U},
    {"beq", Format::B},         {"bge", Format::B},
    {"bgeu", Format::B},        {"blt", Format::B},
    {"bltu", Format::B},        {"bne", Format::B},
    {"div", Format::R},         {"divu", Format::R},
    {"divuw", Format::R},       {"divw", Format::R},
    {"ecall", Format::NONE},    {"jal", Format::J},
    {"jalr", Format::I},        {"lb", Format::I_LOAD},
    {"lbu", Format::I_LOAD},    {"ld", Format::I_LOAD},
    {"lh", Format::I_LOAD},     {"lhu", Format::I_LOAD},
    {"lui", Format::U},         {"lw", Format::I_LOAD},
    {"lwu", Format::I_LOAD},    {"mul", Format::R},
    {"mulh", Format::R},        {"mulhu", Format::R},
    {"mulw", Format::R},        {"or", Format::R},
    {"ori", Format::I},         {"rem", Format::R},
    {"remu", Format::R},        {"remuw", Format::R},
    {"remw", Format::R},        {"sb", Format::S},
    {"sd", Format::S},          {"sh", Format::S},
    {"sll", Format::R},         {"slli", Format::I_SHIFT},
    {"slliw", Format::I_SHIFT}, {"sllw", Format::R},
    {"slt", Format::R},         {"slti", Format::I},
    {"sltiu", Format::I},       {"sltu", Format::R},
    {"sra", Format::R},         {"srai", Format::I_SHIFT},
    {"sraiw", Format::I_SHIFT}, {"sraw", Format::R},
    {"srl", Format::R},         {"srli", Format::I_SHIFT},
    {"srliw", Format::I_SHIFT}, {"srlw", Format::R},
    {"sub", Format::R},         {"subw", Format::R},
    {"sw", Format::S},          {"xor", Format::R},
    {"xori", Format::I},
}};

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

    u64 num_ins = m_code_section.size / 4;
    m_decoded.resize(num_ins);
    for (u64 i = 0; i < num_ins; i++) {
      u32 raw;
      std::memcpy(&raw, m_memory.data() + m_code_section.offset + i * 4,
                  sizeof(raw));
      m_decoded[i] = decode_raw(raw);
    }
  }

  void disassemble_all() {
    for (m_pc = m_code_section.offset;
         m_pc < m_code_section.offset + m_code_section.size; m_pc += 4) {
      disassemble_one();
    }
  }

  void disassemble_one() {
    Ins ins = m_decoded[(m_pc - m_code_section.offset) / 4];

    const OpDef &def = OP_TABLE[ins.op];

    switch (def.format) {
    case Format::R:
      std::println("{} {}, {}, {}", def.mnemonic, REGS[ins.rd], REGS[ins.rs1],
                   REGS[ins.rs2]);
      break;
    case Format::I:
      std::println("{} {}, {}, {}", def.mnemonic, REGS[ins.rd], REGS[ins.rs1],
                   ins.imm);
      break;
    case Format::I_LOAD:
      std::println("{} {}, {}({})", def.mnemonic, REGS[ins.rd], ins.imm,
                   REGS[ins.rs1]);
      break;
    case Format::I_SHIFT:
      std::println("{} {}, {}, {}", def.mnemonic, REGS[ins.rd], REGS[ins.rs1],
                   ins.shamt);
      break;
    case Format::U:
      std::println("{} {}, {}", def.mnemonic, REGS[ins.rd], ins.imm);
      break;
    case Format::S:
      std::println("{} {}, {}({})", def.mnemonic, REGS[ins.rs2], ins.imm,
                   REGS[ins.rs1]);
      break;
    case Format::B:
      std::println("{} {}, {}, {}", def.mnemonic, REGS[ins.rs1], REGS[ins.rs2],
                   ins.imm);
      break;
    case Format::J:
      std::println("{} {}, {}", def.mnemonic, REGS[ins.rd], ins.imm);
      break;
    case Format::NONE:
      std::println("{}", def.mnemonic);
      break;
    }
  }

  void dump() {
    std::print("REGS:");
    for (u64 i = 0; i < 32; i++) {
      std::print(" {}", m_regs[i]);
    }
    std::println();
  }

  void execute() {
    m_pc = m_code_section.entrypoint;

    m_regs[2] = MEMORY_SIZE - 1024; // set the stack pointer

    while (m_pc < m_code_section.offset + m_code_section.size) {
      m_regs[0] = 0; // clear the zero register

      Ins i = m_decoded[(m_pc - m_code_section.offset) / 4];

      switch (i.op) {
      case Op::INVALID: {
        std::println(stderr, "Tried to execute Op::INVALID");
        exit(1);
      }; break;
      case Op::ADD: {
        m_regs[i.rd] = m_regs[i.rs1] + m_regs[i.rs2];
      }; break;
      case Op::ADDI: {
        m_regs[i.rd] = m_regs[i.rs1] + i.imm;
      }; break;
      case Op::ADDIW: {
        m_regs[i.rd] = (i32)m_regs[i.rs1] + (i32)i.imm;
      }; break;
      case Op::ADDW: {
        m_regs[i.rd] = (i32)(m_regs[i.rs1] + m_regs[i.rs2]);
      }; break;
      case Op::AND: {
        m_regs[i.rd] = m_regs[i.rs1] & m_regs[i.rs2];
      }; break;
      case Op::ANDI: {
        std::println(stderr, "ANDI unimplemented");
        exit(1);
      }; break;
      case Op::AUIPC: {
        m_regs[i.rd] = m_pc + ((i64)i.imm << 12);
      }; break;
      case Op::BEQ: {
        if (m_regs[i.rs1] == m_regs[i.rs2]) {
          m_pc += i.imm;
          continue;
        }
      }; break;
      case Op::BGE: {
        if (m_regs[i.rs1] >= m_regs[i.rs2]) {
          m_pc += i.imm;
          continue;
        }
      }; break;
      case Op::BGEU: {
        std::println(stderr, "BGEU unimplemented");
        exit(1);
      }; break;
      case Op::BLT: {
        if (m_regs[i.rs1] < m_regs[i.rs2]) {
          m_pc += i.imm;
          continue;
        }
      }; break;
      case Op::BLTU: {
        if ((u64)m_regs[i.rs1] < (u64)m_regs[i.rs2]) {
          m_pc += i.imm;
          continue;
        }
      }; break;
      case Op::BNE: {
        if (m_regs[i.rs1] != m_regs[i.rs2]) {
          m_pc += i.imm;
          continue;
        }
      }; break;
      case Op::DIV: {
        if (m_regs[i.rs2] == 0) {
          m_regs[i.rd] = -1;
        } else {
          m_regs[i.rd] = m_regs[i.rs1] / m_regs[i.rs2];
        }
      }; break;
      case Op::DIVU: {
        std::println(stderr, "DIVU unimplemented");
        exit(1);
      }; break;
      case Op::DIVUW: {
        if (m_regs[i.rs2] == 0) {
          m_regs[i.rd] = -1LL;
        } else {
          m_regs[i.rd] = (i32)((u32)m_regs[i.rs1] / (u32)m_regs[i.rs2]);
        }
      }; break;
      case Op::DIVW: {
        if (m_regs[i.rs2] == 0) {
          m_regs[i.rd] = (u64)(i64)(-1);
        } else {
          m_regs[i.rd] = (i64)(m_regs[i.rs1] / m_regs[i.rs2]);
        }
      }; break;
      case Op::ECALL: {
        // https://jborza.com/post/2021-05-11-riscv-linux-syscalls/
        switch (m_regs[17]) {
        case 63: { // read
          if (m_regs[10] != 0) {
            std::println(stderr, "read syscall implemented only for stdin.");
            exit(1);
          }

          u64 start = m_regs[11];
          u64 count = m_regs[12];
          u64 bytes_read = 0;

          for (u64 i = 0; i < count; i++) {
            char c;
            if (!std::cin.get(c))
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
            std::println(stderr, "write syscall implemented only for stdout.");
            exit(1);
          }

          u64 start = m_regs[11];
          u64 end = m_regs[11] + m_regs[12];

          for (u64 i = start; i < end; i++) {
            std::cout.put(m_memory[i]);
          }
        }; break;
        case 93: { // exit
          std::println("Program exited with code {}.", m_regs[10]);
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
          std::println(stderr, "Unimplemented syscall: {}", m_regs[17]);
          exit(1);
        }
      }; break;
      case Op::JAL: {
        m_regs[i.rd] = m_pc + 4;
        m_pc += i.imm;
        continue;
      }; break;
      case Op::JALR: {
        u64 target = (m_regs[i.rs1] + i.imm) & ~(u64)1;
        m_regs[i.rd] = m_pc + 4;
        m_pc = target;
        continue;
      }; break;
      case Op::LB: {
        m_regs[i.rd] = (i8)m_memory[m_regs[i.rs1] + i.imm];
      }; break;
      case Op::LBU: {
        m_regs[i.rd] = m_memory[m_regs[i.rs1] + i.imm];
      }; break;
      case Op::LD: {
        m_regs[i.rd] = *(u64 *)&m_memory[m_regs[i.rs1] + i.imm];
      }; break;
      case Op::LH: {
        m_regs[i.rd] = *(i16 *)&m_memory[m_regs[i.rs1] + i.imm];
      }; break;
      case Op::LHU: {
        std::println(stderr, "LHU unimplemented");
        exit(1);
      }; break;
      case Op::LUI: {
        m_regs[i.rd] = (i64)(i32)(i.imm << 12);
      }; break;
      case Op::LW: {
        m_regs[i.rd] = *(i32 *)&m_memory[m_regs[i.rs1] + i.imm];
      }; break;
      case Op::LWU: {
        std::println(stderr, "LWU unimplemented");
        exit(1);
      }; break;
      case Op::MUL: {
        m_regs[i.rd] = m_regs[i.rs1] * m_regs[i.rs2];
      }; break;
      case Op::MULH: {
        i64 a_hi = m_regs[i.rs1] >> 32;
        i64 b_hi = m_regs[i.rs2] >> 32;
        u64 a_lo = (u32)m_regs[i.rs1];
        u64 b_lo = (u32)m_regs[i.rs2];
        u64 p0 = a_lo * b_lo;
        i64 p1 = a_hi * b_lo;
        i64 p2 = b_hi * a_lo;
        i64 p3 = a_hi * b_hi;
        i64 carry = (p0 >> 32);
        i64 mid = p1 + p2 + carry;
        m_regs[i.rd] = p3 + (mid >> 32);
      }; break;
      case Op::MULHU: {
        u64 a = m_regs[i.rs1];
        u64 b = m_regs[i.rs2];
        u64 a_lo = a & 0xffffffff;
        u64 a_hi = a >> 32;
        u64 b_lo = b & 0xffffffff;
        u64 b_hi = b >> 32;
        u64 lo_lo = a_lo * b_lo;
        u64 hi_lo = a_hi * b_lo;
        u64 lo_hi = a_lo * b_hi;
        u64 hi_hi = a_hi * b_hi;
        u64 mid = (lo_lo >> 32) + (hi_lo & 0xffffffff) + (lo_hi & 0xffffffff);
        m_regs[i.rd] = hi_hi + (hi_lo >> 32) + (lo_hi >> 32) + (mid >> 32);
      }; break;
      case Op::MULW: {
        m_regs[i.rd] = (i32)(m_regs[i.rs1] * m_regs[i.rs2]);
      }; break;
      case Op::OR: {
        m_regs[i.rd] = m_regs[i.rs1] | m_regs[i.rs2];
      }; break;
      case Op::ORI: {
        std::println(stderr, "ORI unimplemented");
        exit(1);
      }; break;
      case Op::REM: {
        if (m_regs[i.rs2] == 0) {
          m_regs[i.rd] = m_regs[i.rs1];
        } else {
          m_regs[i.rd] = m_regs[i.rs1] % m_regs[i.rs2];
        }
      }; break;
      case Op::REMU: {
        if (m_regs[i.rs2] == 0) {
          m_regs[i.rd] = m_regs[i.rs1];
        } else {
          m_regs[i.rd] = (u64)m_regs[i.rs1] % (u64)m_regs[i.rs2];
        }
      }; break;
      case Op::REMUW: {
        if (m_regs[i.rs2] == 0) {
          m_regs[i.rd] = (i32)m_regs[i.rs1];
        } else {
          m_regs[i.rd] = (i32)((u32)m_regs[i.rs1] % (u32)m_regs[i.rs2]);
        }
      }; break;
      case Op::REMW: {
        std::println(stderr, "REMW unimplemented");
        exit(1);
      }; break;
      case Op::SB: {
        u64 addr = m_regs[i.rs1] + i.imm;
        m_memory[addr] = m_regs[i.rs2];
      }; break;
      case Op::SD: {
        u64 addr = m_regs[i.rs1] + i.imm;
        *(u64 *)(&m_memory[addr]) = m_regs[i.rs2];
      }; break;
      case Op::SH: {
        u64 addr = m_regs[i.rs1] + i.imm;
        *(u16 *)(&m_memory[addr]) = m_regs[i.rs2];
      }; break;
      case Op::SLL: {
        std::println(stderr, "SLL unimplemented");
        exit(1);
      }; break;
      case Op::SLLI: {
        m_regs[i.rd] = m_regs[i.rs1] << i.shamt;
      }; break;
      case Op::SLLIW: {
        m_regs[i.rd] = (i32)m_regs[i.rs1] << i.shamt;
      }; break;
      case Op::SLLW: {
        m_regs[i.rd] =
            (i32)(u32)((u32)m_regs[i.rs1] << ((u32)m_regs[i.rs2] & 0b11111));
      }; break;
      case Op::SLT: {
        m_regs[i.rd] = (m_regs[i.rs1] < m_regs[i.rs2]) ? 1 : 0;
      }; break;
      case Op::SLTI: {
        std::println(stderr, "SLTI unimplemented");
        exit(1);
      }; break;
      case Op::SLTIU: {
        m_regs[i.rd] = ((u64)m_regs[i.rs1] < (u64)(i64)i.imm) ? 1 : 0;
      }; break;
      case Op::SLTU: {
        m_regs[i.rd] = ((u64)m_regs[i.rs1] < (u64)m_regs[i.rs2]) ? 1 : 0;
      }; break;
      case Op::SRA: {
        std::println(stderr, "SRA unimplemented");
        exit(1);
      }; break;
      case Op::SRAI: {
        m_regs[i.rd] = (i64)m_regs[i.rs1] >> i.shamt;
      }; break;
      case Op::SRAIW: {
        m_regs[i.rd] = (i32)m_regs[i.rs1] >> i.shamt;
      }; break;
      case Op::SRAW: {
        m_regs[i.rd] = ((i32)m_regs[i.rs1]) >> ((u32)m_regs[i.rs2] & 0b11111);
      }; break;
      case Op::SRL: {
        std::println(stderr, "SRL unimplemented");
        exit(1);
      }; break;
      case Op::SRLI: {
        m_regs[i.rd] = (u64)m_regs[i.rs1] >> i.shamt;
      }; break;
      case Op::SRLIW: {
        m_regs[i.rd] = (i32)((u32)m_regs[i.rs1] >> i.shamt);
      }; break;
      case Op::SRLW: {
        m_regs[i.rd] =
            (i32)((u32)m_regs[i.rs1] >> ((u32)m_regs[i.rs2] & 0b11111));
      }; break;
      case Op::SUB: {
        m_regs[i.rd] = m_regs[i.rs1] - m_regs[i.rs2];
      }; break;
      case Op::SUBW: {
        m_regs[i.rd] = (i32)(m_regs[i.rs1] - m_regs[i.rs2]);
      }; break;
      case Op::SW: {
        u64 addr = m_regs[i.rs1] + i.imm;
        *(u32 *)(&m_memory[addr]) = m_regs[i.rs2];
      }; break;
      case Op::XOR: {
        m_regs[i.rd] = m_regs[i.rs1] ^ m_regs[i.rs2];
      }; break;
      case Op::XORI: {
        m_regs[i.rd] = m_regs[i.rs1] ^ i.imm;
      }; break;
      }

      m_pc += 4;
    }
  }

private:
  std::vector<u8> m_memory = std::vector<u8>(MEMORY_SIZE, 0);
  std::vector<Ins> m_decoded;
  u64 m_pc;
  std::array<i64, 32> m_regs{};
  Section m_code_section;

  static Section get_code_section(Elf *elf, GElf_Ehdr ehdr) {
    u64 str_table_index;
    if (elf_getshdrstrndx(elf, &str_table_index) != 0) {
      std::println(stderr, "elf_getshdrstrndx failed: {}", elf_errmsg(-1));
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

    std::println(stderr, "Failed to locate .text");
    exit(1);
  }

  Ins decode_raw(u32 raw) {
    u8 opcode = raw & 0b1111111;

    Ins i;
    i.op = Op::INVALID;

    switch (opcode) {
    case 0b1100011: {
      i.funct3 = (raw >> 12) & 0b111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.rs2 = (raw >> 20) & 0b11111;
      i32 imm_12 = (raw >> 31) & 0b1;
      i32 imm_10_5 = (raw >> 25) & 0b111111;
      i32 imm_4_1 = (raw >> 8) & 0b1111;
      i32 imm_11 = (raw >> 7) & 0b1;
      i.imm =
          (imm_12 << 12) | (imm_11 << 11) | (imm_10_5 << 5) | (imm_4_1 << 1);
      i.imm = (i.imm << 19) >> 19;

      if (i.funct3 == 0b000) {
        i.op = Op::BEQ;
      } else if (i.funct3 == 0b001) {
        i.op = Op::BNE;
      } else if (i.funct3 == 0b100) {
        i.op = Op::BLT;
      } else if (i.funct3 == 0b101) {
        i.op = Op::BGE;
      } else if (i.funct3 == 0b110) {
        i.op = Op::BLTU;
      } else if (i.funct3 == 0b111) {
        i.op = Op::BGEU;
      } else {
        std::println(stderr, "B-type: unrecognized funct3: {:03b}", i.funct3);
        exit(1);
      }
    }; break;
    case 0b0010011: {
      i.rd = (raw >> 7) & 0b11111;
      i.funct3 = (raw >> 12) & 0b111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.imm = ((i32)raw) >> 20;
      i.shamt = (raw >> 20) & 0b111111;
      i.funct6 = (raw >> 26) & 0b111111;

      if (i.funct3 == 0b000) {
        i.op = Op::ADDI;
      } else if (i.funct3 == 0b001) {
        if (i.funct6 == 0b000000) {
          i.op = Op::SLLI;
        } else {
          std::println(stderr,
                       "I-type 1: funct3=001: unrecognized funct6: {:b}",
                       i.funct6);
          exit(1);
        }
      } else if (i.funct3 == 0b010) {
        i.op = Op::SLTI;
      } else if (i.funct3 == 0b011) {
        i.op = Op::SLTIU;
      } else if (i.funct3 == 0b100) {
        i.op = Op::XORI;
      } else if (i.funct3 == 0b101) {
        i.shamt = (raw >> 20) & 0b111111;
        i.funct6 = (raw >> 26) & 0b111111;

        if (i.funct6 == 0b000000) {
          i.op = Op::SRLI;
        } else if (i.funct6 == 0b010000) {
          i.op = Op::SRAI;
        } else {
          std::println(stderr,
                       "I-type 1: funct3=101: unrecognized funct6: {:b}",
                       i.funct6);
          exit(1);
        }
      } else if (i.funct3 == 0b110) {
        i.op = Op::ORI;
      } else if (i.funct3 == 0b111) {
        i.op = Op::ANDI;
      } else {
        std::println(stderr, "I-type 1: unrecognized funct3: {:03b}", i.funct3);
        exit(1);
      }
    }; break;
    case 0b0000011: {
      i.rd = (raw >> 7) & 0b11111;
      i.funct3 = (raw >> 12) & 0b111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.imm = ((i32)raw) >> 20;

      if (i.funct3 == 0b000) {
        i.op = Op::LB;
      } else if (i.funct3 == 0b001) {
        i.op = Op::LH;
      } else if (i.funct3 == 0b010) {
        i.op = Op::LW;
      } else if (i.funct3 == 0b011) {
        i.op = Op::LD;
      } else if (i.funct3 == 0b100) {
        i.op = Op::LBU;
      } else if (i.funct3 == 0b101) {
        i.op = Op::LHU;
      } else if (i.funct3 == 0b110) {
        i.op = Op::LWU;
      } else {
        std::println(stderr, "I-type 2: unrecognized funct3: {:03b}", i.funct3);
        exit(1);
      }
    }; break;
    case 0b1100111: {
      i.rd = (raw >> 7) & 0b11111;
      i.funct3 = (raw >> 12) & 0b111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.imm = ((i32)raw) >> 20;
      i.op = Op::JALR;
    }; break;
    case 0b1110011: {
      i.rd = (raw >> 7) & 0b11111;
      i.funct3 = (raw >> 12) & 0b111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.imm = ((i32)raw) >> 20;

      if (i.funct3 == 0b000) {
        if (i.imm == 0) {
          i.op = Op::ECALL;
        } else {
          std::println(stderr, "I-type 4: funct3=000 unrecognized imm: {:b}",
                       i.imm);
          exit(1);
        }
      } else {
        std::println(stderr, "I-type 4: unrecognized funct3: {:03b}", i.funct3);
        exit(1);
      }
    }; break;
    case 0b1101111: {
      i.rd = (raw >> 7) & 0b11111;
      i32 imm_20 = (raw >> 31) & 0b1;
      i32 imm_10_1 = (raw >> 21) & 0b1111111111;
      i32 imm_11 = (raw >> 20) & 0b1;
      i32 imm_19_12 = (raw >> 12) & 0b11111111;
      i.imm =
          (imm_20 << 20) | (imm_19_12 << 12) | (imm_11 << 11) | (imm_10_1 << 1);
      i.imm = (i.imm << 11) >> 11;
      i.op = Op::JAL;
    }; break;
    case 0b0110011: {
      i.rd = (raw >> 7) & 0b11111;
      i.funct3 = (raw >> 12) & 0b111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.rs2 = (raw >> 20) & 0b11111;
      i.funct7 = (u8)((raw >> 25) & 0b1111111);

      if (i.funct3 == 0b000) {
        if (i.funct7 == 0b0000000) {
          i.op = Op::ADD;
        } else if (i.funct7 == 0b0100000) {
          i.op = Op::SUB;
        } else if (i.funct7 == 0b0000001) {
          i.op = Op::MUL;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b000: unrecognized funct7: {:b}",
                       i.funct7);
          exit(1);
        }
      } else if (i.funct3 == 0b001) {
        if (i.funct7 == 0b0000000) {
          i.op = Op::SLL;
        } else if (i.funct7 == 0b0000001) {
          i.op = Op::MULH;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b001: unrecognized funct7: {:b}",
                       i.funct7);
          exit(1);
        }
      } else if (i.funct3 == 0b010) {
        if (i.funct7 == 0b0000000) {
          i.op = Op::SLT;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b010: unrecognized funct7: {:b}",
                       i.funct7);
          exit(1);
        }
      } else if (i.funct3 == 0b011) {
        if (i.funct7 == 0b0000000) {
          i.op = Op::SLTU;
        } else if (i.funct7 == 0b0000001) {
          i.op = Op::MULHU;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b011: unrecognized funct7: {:b}",
                       i.funct7);
          exit(1);
        }
      } else if (i.funct3 == 0b100) {
        if (i.funct7 == 0b0000000) {
          i.op = Op::XOR;
        } else if (i.funct7 == 0b0000001) {
          i.op = Op::DIV;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b100: unrecognized funct7: {:b}",
                       i.funct7);
          exit(1);
        }
      } else if (i.funct3 == 0b101) {
        if (i.funct7 == 0b0000000) {
          i.op = Op::SRL;
        } else if (i.funct7 == 0b0000001) {
          i.op = Op::DIVU;
        } else if (i.funct7 == 0b0100000) {
          i.op = Op::SRA;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b101: unrecognized funct7: {:b}",
                       i.funct7);
          exit(1);
        }
      } else if (i.funct3 == 0b110) {
        if (i.funct7 == 0b0000001) {
          i.op = Op::REM;
        } else if (i.funct7 == 0b0000000) {
          i.op = Op::OR;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b110: unrecognized funct7: {:b}",
                       i.funct7);
          exit(1);
        }
      } else if (i.funct3 == 0b111) {
        if (i.funct7 == 0b000) {
          i.op = Op::AND;
        } else if (i.funct7 == 0b001) {
          i.op = Op::REMU;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b111: unrecognized funct7: {:b}",
                       i.funct7);
          exit(1);
        }
      } else {
        std::println(stderr, "R-type 1: unrecognized funct3: {:03b}", i.funct3);
        exit(1);
      }
    }; break;
    case 0b0101111:
      std::println(stderr, "A extension not implemented yet.");
      exit(1);
    case 0b0111011: {
      i.rd = (raw >> 7) & 0b11111;
      i.funct3 = (raw >> 12) & 0b111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.rs2 = (raw >> 20) & 0b11111;
      i.funct7 = (u8)((raw >> 25) & 0b1111111);

      if (i.funct3 == 0b000) {
        if (i.funct7 == 0b0000000) {
          i.op = Op::ADDW;
        } else if (i.funct7 == 0b0100000) {
          i.op = Op::SUBW;
        } else if (i.funct7 == 0b0000001) {
          i.op = Op::MULW;
        } else {
          std::println(stderr,
                       "R-type 3: funct3=000: unrecognized funct7: {:b}",
                       i.funct7);
          exit(1);
        }
      } else if (i.funct3 == 0b001) {
        i.op = Op::SLLW;
      } else if (i.funct3 == 0b100) {
        i.op = Op::DIVW;
      } else if (i.funct3 == 0b101) {
        if (i.funct7 == 0b0000000) {
          i.op = Op::SRLW;
        } else if (i.funct7 == 0b0100000) {
          i.op = Op::SRAW;
        } else if (i.funct7 == 0b0000001) {
          i.op = Op::DIVUW;
        } else {
          std::println(stderr,
                       "R-type 3: funct3=101: unrecognized funct7: {:b}",
                       i.funct7);
          exit(1);
        }
      } else if (i.funct3 == 0b110) {
        i.op = Op::REMW;
      } else if (i.funct3 == 0b111) {
        i.op = Op::REMUW;
      } else {
        std::println(stderr, "R-type 3: unrecognized funct3: {:03b}", i.funct3);
        exit(1);
      }
    }; break;
    case 0b0011011: {
      i.rd = (raw >> 7) & 0b11111;
      i.funct3 = (raw >> 12) & 0b111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.imm = ((i32)raw) >> 20;
      i.shamt = (raw >> 20) & 0b11111;
      i.funct7 = (raw >> 25) & 0b1111111;

      if (i.funct3 == 0b000) {
        i.op = Op::ADDIW;
      } else if (i.funct3 == 0b001) {
        i.op = Op::SLLIW;
      } else if (i.funct3 == 0b101) {
        if (i.funct7 == 0b0000000) {
          i.op = Op::SRLIW;
        } else if (i.funct7 == 0b0100000) {
          i.op = Op::SRAIW;
        } else {
          std::println(stderr,
                       "R-type 4: funct3=101: unrecognized funct7: {:b}",
                       i.funct7);
          exit(1);
        }
      } else {
        std::println(stderr, "R-type 4: unrecognized funct3: {:03b}", i.funct3);
        exit(1);
      }
    }; break;
    case 0b0000111: {
      std::println(stderr, "F extension not implemented yet.");
      exit(1);
    }; break;
    case 0b0100111: {
      std::println(stderr, "F extension not implemented yet.");
      exit(1);
    }; break;
    case 0b0100011: {
      i.funct3 = (raw >> 12) & 0b111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.rs2 = (raw >> 20) & 0b11111;
      i32 imm_11_5 = (raw >> 25) & 0b1111111;
      i32 imm_4_0 = (raw >> 7) & 0b11111;
      i.imm = (imm_11_5 << 5) | imm_4_0;
      i.imm = (i.imm << 20) >> 20;

      if (i.funct3 == 0b000) {
        i.op = Op::SB;
      } else if (i.funct3 == 0b001) {
        i.op = Op::SH;
      } else if (i.funct3 == 0b010) {
        i.op = Op::SW;
      } else if (i.funct3 == 0b011) {
        i.op = Op::SD;
      } else {
        std::println(stderr, "S-type: unrecognized funct3: {:03b}", i.funct3);
        exit(1);
      }
    }; break;
    case 0b0110111: {
      i.rd = (raw >> 7) & 0b11111;
      i.imm = raw >> 12;
      i.op = Op::LUI;
    }; break;
    case 0b0010111: {
      i.rd = (raw >> 7) & 0b11111;
      i.imm = raw >> 12;
      i.op = Op::AUIPC;
    }; break;
    default:
      std::println(stderr, "Unrecognized opcode: {:07b}", opcode);
      exit(1);
    }

    return i;
  }
};

int main(int argc, char *argv[]) {
  if (elf_version(EV_CURRENT) == EV_NONE) {
    std::println(stderr, "Failed to initialize libelf: {}", elf_errmsg(-1));
    return 1;
  }

  const char *path = nullptr;
  for (int i = 1; i < argc; i++) {
    path = argv[i];
  }

  if (path == nullptr) {
    std::println(stderr, "Usage: {} <path>", argv[0]);
    return 1;
  }

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    std::println(stderr, "Failed to open: {}", path);
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
  std::println("END DISASSEMBLY");

  r.execute();
}
