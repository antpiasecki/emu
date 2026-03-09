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
#include <sys/mman.h>
#include <sys/time.h>
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

static constexpr u64 MEMORY_SIZE =
    2ULL * 1024 * 1024 * 1024; // should be enough

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
  C_ADDI,
  C_ADDI16SP,
  C_ADDI4SPN,
  C_LI,
  C_LUI,
  C_MV,
  C_SDSP,
  C_SLLI,
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
  XORI,
};

struct Ins {
  Op op;
  u8 rd;
  u8 rs1;
  union {
    u8 rs2;
    u8 shamt;
  };
  i32 imm;
};

struct Section {
  u64 offset;
  u64 size;
  u64 entrypoint;
};

enum class Format { NONE, R, I, I_LOAD, I_SHIFT, S, B, U, J, CI, CSS, CR };

struct OpDef {
  const char *mnemonic;
  Format format;
};

static constexpr std::array<OpDef, 71> OP_TABLE = {{
    {"???", Format::NONE},      {"add", Format::R},
    {"addi", Format::I},        {"addiw", Format::I},
    {"addw", Format::R},        {"and", Format::R},
    {"andi", Format::I},        {"auipc", Format::U},
    {"beq", Format::B},         {"bge", Format::B},
    {"bgeu", Format::B},        {"blt", Format::B},
    {"bltu", Format::B},        {"bne", Format::B},
    {"c.addi", Format::CI},     {"c.addi16sp", Format::CI},
    {"c.addi4spn", Format::CI}, {"c.li", Format::CI},
    {"c.lui", Format::CI},      {"c.mv", Format::CR},
    {"c.sdsp", Format::CSS},    {"c.slli", Format::CI},
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
    m_memory = (u8 *)mmap(nullptr, MEMORY_SIZE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m_memory == MAP_FAILED) {
      std::println(stderr, "Failed to mmap memory");
      exit(1);
    }

    Elf *elf =
        elf_memory(const_cast<char *>(exe_bytes.data()), exe_bytes.size());
    GElf_Ehdr ehdr;
    gelf_getehdr(elf, &ehdr);

    if (ehdr.e_machine != EM_RISCV) {
      std::println(stderr, "ehdr.e_machine != EM_RISCV");
      exit(1);
    }

    m_code_section = get_code_section(elf, ehdr);
    m_pc = m_code_section.offset;

    for (u64 i = 0; i < ehdr.e_phnum; i++) {
      GElf_Phdr phdr;
      gelf_getphdr(elf, i, &phdr);
      if (phdr.p_type == PT_LOAD) {
        std::copy_n(exe_bytes.data() + phdr.p_offset, phdr.p_filesz,
                    m_memory + phdr.p_vaddr);
      }
    }
    elf_end(elf);

    u64 num_ins = m_code_section.size / 4;
    m_decoded.resize(num_ins);
    for (u64 i = 0; i < num_ins; i++) {
      u32 raw;
      std::memcpy(&raw, m_memory + m_code_section.offset + i * 4, sizeof(raw));
      m_decoded[i] = decode_raw(raw);
    }
  }

  ~RISCV64() { munmap(m_memory, MEMORY_SIZE); }

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
    case Format::CI:
      std::println("{} {}, {}", def.mnemonic, REGS[ins.rd], ins.imm);
      break;
    case Format::CSS:
      std::println("{} {}, {}(sp)", def.mnemonic, REGS[ins.rs2], ins.imm);
      break;
    case Format::CR:
      std::println("{} {}, {}", def.mnemonic, REGS[ins.rd], REGS[ins.rs2]);
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

  void push_u64(u64 x) {
    m_regs[2] -= 8;
    *(u64 *)(m_memory + m_regs[2]) = x;
  }

  void execute() {
    m_pc = m_code_section.entrypoint;

    // set up the stack
    i64 &sp = m_regs[2];
    sp = MEMORY_SIZE - 1024;

    // push "program"
    const char *prog = "program";
    u64 len = strlen(prog) + 1;
    sp -= len;
    std::memcpy(m_memory + sp, prog, len);
    u64 prog_ptr = sp;
    sp &= ~15;

    // auxv = {0}
    push_u64(0);
    push_u64(0);
    // envp = {0}
    push_u64(0);
    // argv = {prog_ptr, 0}
    push_u64(0);
    push_u64(prog_ptr);
    // argc = 1
    push_u64(1);

    while (m_pc < MEMORY_SIZE) {
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
        m_regs[i.rd] = m_regs[i.rs1] & i.imm;
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
        i32 a = m_regs[i.rs1];
        i32 b = m_regs[i.rs2];
        if (b == 0) {
          m_regs[i.rd] = (u64)(i64)(-1);
        } else if (a == INT32_MIN && b == -1) {
          m_regs[i.rd] = (i64)INT32_MIN;
        } else {
          m_regs[i.rd] = (i64)(a / b);
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
          i64 tv_addr = m_regs[10];
          i64 tz_addr = m_regs[11];

          struct timeval tv;
          struct timezone tz;

          i32 ret = gettimeofday(&tv, (tz_addr != 0) ? &tz : nullptr);
          if (ret == 0) {
            memcpy(&m_memory[tv_addr], &tv, sizeof(tv));
            if (tz_addr != 0) {
              memcpy(&m_memory[tz_addr], &tz, sizeof(tz));
            }
            m_regs[10] = 0;
          } else {
            m_regs[10] = -errno;
          }
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
      default: {
        std::println(stderr, "Unrecognized Op: {}", (u64)i.op);
        exit(1);
      }; break;
      }

      m_pc += 4;
    }
  }

private:
  u8 *m_memory;
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
    if ((raw & 0b11) != 0b11) {
      return decode_raw_16bit((u16)raw);
    } else {
      return decode_raw_32bit(raw);
    }
  }

  // https://docs.riscv.org/reference/isa/unpriv/c-st-ext.html#27-8-rvc-instruction-set-listings
  Ins decode_raw_16bit(u16 raw) {
    u8 opcode = raw & 0b11;
    u32 funct3 = (raw >> 13) & 0b111;

    Ins i;
    i.op = Op::INVALID;

    i.rd = (raw >> 7) & 0b11111;

    switch (opcode) {
    case 0b00: {
      switch (funct3) {
      case 0b000: {
        if (raw == 0) {
          std::println(stderr, "C: illegal instruction (all zeros)");
          exit(1);
        }

        i.rd = ((raw >> 2) & 0b111) + 8;
        i.rs1 = 2;
        i.imm = (((raw >> 11) & 0b11) << 4) | (((raw >> 7) & 0b1111) << 6) |
                (((raw >> 6) & 0b1) << 2) | (((raw >> 5) & 0b1) << 3);
        i.op = Op::C_ADDI4SPN;
      }; break;
      default: {
        std::println(stderr, "C: opcode=00: unrecognized funct3: {:03b}",
                     funct3);
        exit(1);
      }; break;
      }
    }; break;
    case 0b01: {
      switch (funct3) {
      case 0b000: {
        i.imm = ((raw >> 2) & 0b11111) | (((raw >> 12) & 0b1) << 5);
        i.imm = (i.imm << 26) >> 26;
        i.op = Op::C_ADDI;
      }; break;
      case 0b010: {
        i.imm = ((raw >> 2) & 0b11111) | (((raw >> 12) & 0b1) << 5);
        i.imm = (i.imm << 26) >> 26;
        i.op = Op::C_LI;
      }; break;
      case 0b011: {
        if (i.rd == 2) {
          i.imm = (((raw >> 12) & 0b1) << 9) | (((raw >> 3) & 0b11) << 7) |
                  (((raw >> 5) & 0b1) << 6) | (((raw >> 2) & 0b1) << 5) |
                  (((raw >> 6) & 0b1) << 4);
          i.imm = (i.imm << 22) >> 22;
          i.op = Op::C_ADDI16SP;
        } else {
          i.imm = (((raw >> 12) & 0b1) << 17) | (((raw >> 2) & 0b11111) << 12);
          i.imm = (i.imm << 14) >> 14;
          i.op = Op::C_LUI;
        }

      }; break;
      default: {
        std::println(stderr, "C: opcode=01: unrecognized funct3: {:03b}",
                     funct3);
        exit(1);
      }; break;
      }
    }; break;
    case 0b10: {
      switch (funct3) {
      case 0b000: {
        i.imm = ((raw >> 2) & 0b11111) | (((raw >> 12) & 0b1) << 5);
        i.op = Op::C_SLLI;
      }; break;
      case 0b010: {
        std::println(stderr, "F extension not implemented yet.");
        exit(1);
      }; break;
      case 0b100: {
        bool bit12 = (raw >> 12) & 0b1;
        i.rs2 = (raw >> 2) & 0b11111;

        if (bit12 == 0) {
          if (i.rs2 == 0) {
            std::println(
                stderr,
                "C: opcode=10: funct3=100: bit12=0: rs2=0: unimplemented");
            exit(1);
          } else {
            i.rs1 = 0;
            i.op = Op::C_MV;
          }
        } else {
          std::println(stderr,
                       "C: opcode=10: funct3=100: bit12=1: unimplemented");
          exit(1);
        }
      }; break;
      case 0b111: {
        i.rs2 = (raw >> 2) & 0b11111;
        i.imm = (((raw >> 10) & 0b111) << 3) | (((raw >> 7) & 0b111) << 6);
        i.op = Op::C_SDSP;
      }; break;
      default: {
        std::println(stderr, "C: opcode=10: unrecognized funct3: {:03b}",
                     funct3);
        exit(1);
      }; break;
      }
    }; break;
    default: {
      std::println(stderr, "C: unrecognized opcode: {:02b}", opcode);
      exit(1);
    }; break;
    }

    return i;
  }

  // https://docs.riscv.org/reference/isa/unpriv/rv-32-64g.html
  Ins decode_raw_32bit(u32 raw) {
    u8 opcode = raw & 0b1111111;

    Ins i;
    i.op = Op::INVALID;

    switch (opcode) {
    case 0b1100011: {
      u8 funct3 = (raw >> 12) & 0b111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.rs2 = (raw >> 20) & 0b11111;
      i32 imm_12 = (raw >> 31) & 0b1;
      i32 imm_10_5 = (raw >> 25) & 0b111111;
      i32 imm_4_1 = (raw >> 8) & 0b1111;
      i32 imm_11 = (raw >> 7) & 0b1;
      i.imm =
          (imm_12 << 12) | (imm_11 << 11) | (imm_10_5 << 5) | (imm_4_1 << 1);
      i.imm = (i.imm << 19) >> 19;

      if (funct3 == 0b000) {
        i.op = Op::BEQ;
      } else if (funct3 == 0b001) {
        i.op = Op::BNE;
      } else if (funct3 == 0b100) {
        i.op = Op::BLT;
      } else if (funct3 == 0b101) {
        i.op = Op::BGE;
      } else if (funct3 == 0b110) {
        i.op = Op::BLTU;
      } else if (funct3 == 0b111) {
        i.op = Op::BGEU;
      } else {
        std::println(stderr, "B-type: unrecognized funct3: {:03b}", funct3);
        exit(1);
      }
    }; break;
    case 0b0010011: {
      u8 funct3 = (raw >> 12) & 0b111;
      u8 funct6 = (raw >> 26) & 0b111111;
      i.rd = (raw >> 7) & 0b11111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.imm = ((i32)raw) >> 20;
      i.shamt = (raw >> 20) & 0b111111;

      if (funct3 == 0b000) {
        i.op = Op::ADDI;
      } else if (funct3 == 0b001) {
        if (funct6 == 0b000000) {
          i.op = Op::SLLI;
        } else {
          std::println(stderr,
                       "I-type 1: funct3=001: unrecognized funct6: {:b}",
                       funct6);
          exit(1);
        }
      } else if (funct3 == 0b010) {
        i.op = Op::SLTI;
      } else if (funct3 == 0b011) {
        i.op = Op::SLTIU;
      } else if (funct3 == 0b100) {
        i.op = Op::XORI;
      } else if (funct3 == 0b101) {
        i.shamt = (raw >> 20) & 0b111111;
        funct6 = (raw >> 26) & 0b111111;

        if (funct6 == 0b000000) {
          i.op = Op::SRLI;
        } else if (funct6 == 0b010000) {
          i.op = Op::SRAI;
        } else {
          std::println(stderr,
                       "I-type 1: funct3=101: unrecognized funct6: {:b}",
                       funct6);
          exit(1);
        }
      } else if (funct3 == 0b110) {
        i.op = Op::ORI;
      } else if (funct3 == 0b111) {
        i.op = Op::ANDI;
      } else {
        std::println(stderr, "I-type 1: unrecognized funct3: {:03b}", funct3);
        exit(1);
      }
    }; break;
    case 0b0000011: {
      u8 funct3 = (raw >> 12) & 0b111;
      i.rd = (raw >> 7) & 0b11111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.imm = ((i32)raw) >> 20;

      if (funct3 == 0b000) {
        i.op = Op::LB;
      } else if (funct3 == 0b001) {
        i.op = Op::LH;
      } else if (funct3 == 0b010) {
        i.op = Op::LW;
      } else if (funct3 == 0b011) {
        i.op = Op::LD;
      } else if (funct3 == 0b100) {
        i.op = Op::LBU;
      } else if (funct3 == 0b101) {
        i.op = Op::LHU;
      } else if (funct3 == 0b110) {
        i.op = Op::LWU;
      } else {
        std::println(stderr, "I-type 2: unrecognized funct3: {:03b}", funct3);
        exit(1);
      }
    }; break;
    case 0b1100111: {
      i.rd = (raw >> 7) & 0b11111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.imm = ((i32)raw) >> 20;
      i.op = Op::JALR;
    }; break;
    case 0b1110011: {
      u8 funct3 = (raw >> 12) & 0b111;
      i.rd = (raw >> 7) & 0b11111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.imm = ((i32)raw) >> 20;

      if (funct3 == 0b000) {
        if (i.imm == 0b000000000000) {
          i.op = Op::ECALL;
        } else {
          std::println(stderr, "I-type 4: funct3=000 unrecognized imm: {:b}",
                       i.imm);
          exit(1);
        }
      } else {
        std::println(stderr, "I-type 4: unrecognized funct3: {:03b}", funct3);
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
      u8 funct3 = (raw >> 12) & 0b111;
      u8 funct7 = (u8)((raw >> 25) & 0b1111111);
      i.rd = (raw >> 7) & 0b11111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.rs2 = (raw >> 20) & 0b11111;

      if (funct3 == 0b000) {
        if (funct7 == 0b0000000) {
          i.op = Op::ADD;
        } else if (funct7 == 0b0100000) {
          i.op = Op::SUB;
        } else if (funct7 == 0b0000001) {
          i.op = Op::MUL;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b000: unrecognized funct7: {:b}",
                       funct7);
          exit(1);
        }
      } else if (funct3 == 0b001) {
        if (funct7 == 0b0000000) {
          i.op = Op::SLL;
        } else if (funct7 == 0b0000001) {
          i.op = Op::MULH;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b001: unrecognized funct7: {:b}",
                       funct7);
          exit(1);
        }
      } else if (funct3 == 0b010) {
        if (funct7 == 0b0000000) {
          i.op = Op::SLT;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b010: unrecognized funct7: {:b}",
                       funct7);
          exit(1);
        }
      } else if (funct3 == 0b011) {
        if (funct7 == 0b0000000) {
          i.op = Op::SLTU;
        } else if (funct7 == 0b0000001) {
          i.op = Op::MULHU;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b011: unrecognized funct7: {:b}",
                       funct7);
          exit(1);
        }
      } else if (funct3 == 0b100) {
        if (funct7 == 0b0000000) {
          i.op = Op::XOR;
        } else if (funct7 == 0b0000001) {
          i.op = Op::DIV;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b100: unrecognized funct7: {:b}",
                       funct7);
          exit(1);
        }
      } else if (funct3 == 0b101) {
        if (funct7 == 0b0000000) {
          i.op = Op::SRL;
        } else if (funct7 == 0b0000001) {
          i.op = Op::DIVU;
        } else if (funct7 == 0b0100000) {
          i.op = Op::SRA;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b101: unrecognized funct7: {:b}",
                       funct7);
          exit(1);
        }
      } else if (funct3 == 0b110) {
        if (funct7 == 0b0000001) {
          i.op = Op::REM;
        } else if (funct7 == 0b0000000) {
          i.op = Op::OR;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b110: unrecognized funct7: {:b}",
                       funct7);
          exit(1);
        }
      } else if (funct3 == 0b111) {
        if (funct7 == 0b0000000) {
          i.op = Op::AND;
        } else if (funct7 == 0b0000001) {
          i.op = Op::REMU;
        } else {
          std::println(stderr,
                       "R-type 1: funct3=0b111: unrecognized funct7: {:b}",
                       funct7);
          exit(1);
        }
      } else {
        std::println(stderr, "R-type 1: unrecognized funct3: {:03b}", funct3);
        exit(1);
      }
    }; break;
    case 0b0101111:
      std::println(stderr, "A extension not implemented yet.");
      exit(1);
    case 0b0111011: {
      u8 funct3 = (raw >> 12) & 0b111;
      u8 funct7 = (u8)((raw >> 25) & 0b1111111);
      i.rd = (raw >> 7) & 0b11111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.rs2 = (raw >> 20) & 0b11111;

      if (funct3 == 0b000) {
        if (funct7 == 0b0000000) {
          i.op = Op::ADDW;
        } else if (funct7 == 0b0100000) {
          i.op = Op::SUBW;
        } else if (funct7 == 0b0000001) {
          i.op = Op::MULW;
        } else {
          std::println(stderr,
                       "R-type 3: funct3=000: unrecognized funct7: {:b}",
                       funct7);
          exit(1);
        }
      } else if (funct3 == 0b001) {
        i.op = Op::SLLW;
      } else if (funct3 == 0b100) {
        i.op = Op::DIVW;
      } else if (funct3 == 0b101) {
        if (funct7 == 0b0000000) {
          i.op = Op::SRLW;
        } else if (funct7 == 0b0100000) {
          i.op = Op::SRAW;
        } else if (funct7 == 0b0000001) {
          i.op = Op::DIVUW;
        } else {
          std::println(stderr,
                       "R-type 3: funct3=101: unrecognized funct7: {:b}",
                       funct7);
          exit(1);
        }
      } else if (funct3 == 0b110) {
        i.op = Op::REMW;
      } else if (funct3 == 0b111) {
        i.op = Op::REMUW;
      } else {
        std::println(stderr, "R-type 3: unrecognized funct3: {:03b}", funct3);
        exit(1);
      }
    }; break;
    case 0b0011011: {
      u8 funct3 = (raw >> 12) & 0b111;
      u8 funct7 = (raw >> 25) & 0b1111111;
      i.rd = (raw >> 7) & 0b11111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.imm = ((i32)raw) >> 20;
      i.shamt = (raw >> 20) & 0b11111;

      if (funct3 == 0b000) {
        i.op = Op::ADDIW;
      } else if (funct3 == 0b001) {
        i.op = Op::SLLIW;
      } else if (funct3 == 0b101) {
        if (funct7 == 0b0000000) {
          i.op = Op::SRLIW;
        } else if (funct7 == 0b0100000) {
          i.op = Op::SRAIW;
        } else {
          std::println(stderr,
                       "R-type 4: funct3=101: unrecognized funct7: {:b}",
                       funct7);
          exit(1);
        }
      } else {
        std::println(stderr, "R-type 4: unrecognized funct3: {:03b}", funct3);
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
      u8 funct3 = (raw >> 12) & 0b111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.rs2 = (raw >> 20) & 0b11111;
      i32 imm_11_5 = (raw >> 25) & 0b1111111;
      i32 imm_4_0 = (raw >> 7) & 0b11111;
      i.imm = (imm_11_5 << 5) | imm_4_0;
      i.imm = (i.imm << 20) >> 20;

      if (funct3 == 0b000) {
        i.op = Op::SB;
      } else if (funct3 == 0b001) {
        i.op = Op::SH;
      } else if (funct3 == 0b010) {
        i.op = Op::SW;
      } else if (funct3 == 0b011) {
        i.op = Op::SD;
      } else {
        std::println(stderr, "S-type: unrecognized funct3: {:03b}", funct3);
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
