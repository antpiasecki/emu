// https://docs.riscv.org/reference/isa/unpriv/rv-32-64g.html
// https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf
// TODO: convert C instructions to normal ones at parse time?

#if !defined(__cplusplus) || __cplusplus < 202302L
#error "C++23 or later is required. Either get a newer compiler " \
       "or manually edit this file to include fmtlib (https://github.com/fmtlib/fmt) " \
       "instead of <print> and remove this check."
#endif

#include <cassert>
#include <cstring>
#include <fstream>
#include <gelf.h>
#include <iostream>
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
  AMOADD_D,
  AMOADD_W,
  AMOMAXU_D,
  AMOMAXU_W,
  AMOOR_W,
  AMOSWAP_D,
  AMOSWAP_W,
  AND,
  ANDI,
  AUIPC,
  BEQ,
  BGE,
  BGEU,
  BLT,
  BLTU,
  BNE,
  C_ADD,
  C_ADDI,
  C_ADDIW,
  C_ADDI16SP,
  C_ADDI4SPN,
  C_ADDW,
  C_AND,
  C_ANDI,
  C_BEQZ,
  C_BNEZ,
  C_EBREAK,
  C_FLD,
  C_FLDSP,
  C_FSD,
  C_FSDSP,
  C_J,
  C_JALR,
  C_JR,
  C_LD,
  C_LDSP,
  C_LI,
  C_LUI,
  C_LW,
  C_LWSP,
  C_MV,
  C_OR,
  C_SD,
  C_SDSP,
  C_SLLI,
  C_SRAI,
  C_SRLI,
  C_SUB,
  C_SUBW,
  C_SW,
  C_SWSP,
  C_XOR,
  CSRRS,
  CSRRSI,
  DIV,
  DIVU,
  DIVUW,
  DIVW,
  ECALL,
  FADD_D,
  FCLASS_D,
  FCVT_D_W,
  FCVT_D_WU,
  FENCE,
  FENCE_TSO,
  FLD,
  FLW,
  FMUL_D,
  FMV_D_X,
  FMV_W_X,
  FMV_X_D,
  FSD,
  FSGNJ_D,
  FSGNJN_D,
  FSGNJX_D,
  FSGNJ_S,
  FSGNJN_S,
  FSGNJX_S,
  FSW,
  JAL,
  JALR,
  LB,
  LBU,
  LD,
  LH,
  LHU,
  LR_D,
  LR_W,
  LUI,
  LW,
  LWU,
  MUL,
  MULH,
  MULHU,
  MULW,
  OR,
  ORI,
  PAUSE,
  REM,
  REMU,
  REMUW,
  REMW,
  SB,
  SC_D,
  SC_W,
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

  NUM_OPS
};

struct Ins {
  Op op;
  u8 length;

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

enum class Format {
  NONE,
  R,
  I,
  I_LOAD,
  I_SHIFT,
  S,
  B,
  CB,
  U,
  J,
  CI,
  CJ,
  CSS,
  CR,
  CR1,
  CL,
  R_ATOMIC,
  R_ATOMIC_LR,
  CSR,
  CSRI
};

struct OpDef {
  const char *mnemonic;
  Format format;
};

static constexpr auto OP_TABLE = std::to_array<OpDef>({
    {"???", Format::NONE},
    {"add", Format::R},
    {"addi", Format::I},
    {"addiw", Format::I},
    {"addw", Format::R},
    {"amoadd.d", Format::R_ATOMIC},
    {"amoadd.w", Format::R_ATOMIC},
    {"amomaxu.d", Format::R_ATOMIC},
    {"amomaxu.w", Format::R_ATOMIC},
    {"amoor.w", Format::R_ATOMIC},
    {"amoswap.d", Format::R_ATOMIC},
    {"amoswap.w", Format::R_ATOMIC},
    {"and", Format::R},
    {"andi", Format::I},
    {"auipc", Format::U},
    {"beq", Format::B},
    {"bge", Format::B},
    {"bgeu", Format::B},
    {"blt", Format::B},
    {"bltu", Format::B},
    {"bne", Format::B},
    {"c.add", Format::CR},
    {"c.addi", Format::CI},
    {"c.addiw", Format::CI},
    {"c.addi16sp", Format::CI},
    {"c.addi4spn", Format::CI},
    {"c.addw", Format::CR},
    {"c.and", Format::CR},
    {"c.andi", Format::CI},
    {"c.beqz", Format::CB},
    {"c.bnez", Format::CB},
    {"c.ebreak", Format::NONE},
    {"c.fld", Format::CL},
    {"c.fldsp", Format::CI},
    {"c.fsd", Format::S},
    {"c.fsdsp", Format::CSS},
    {"c.j", Format::CJ},
    {"c.jalr", Format::CR1},
    {"c.jr", Format::CR1},
    {"c.ld", Format::CL},
    {"c.ldsp", Format::CL},
    {"c.li", Format::CI},
    {"c.lui", Format::CI},
    {"c.lw", Format::CL},
    {"c.lwsp", Format::CL},
    {"c.mv", Format::CR},
    {"c.or", Format::CR},
    {"c.sd", Format::S},
    {"c.sdsp", Format::CSS},
    {"c.slli", Format::CI},
    {"c.srai", Format::CI},
    {"c.srli", Format::CI},
    {"c.sub", Format::CR},
    {"c.subw", Format::CR},
    {"c.sw", Format::S},
    {"c.swsp", Format::CSS},
    {"c.xor", Format::CR},
    {"csrrs", Format::CSR},
    {"csrrsi", Format::CSRI},
    {"div", Format::R},
    {"divu", Format::R},
    {"divuw", Format::R},
    {"divw", Format::R},
    {"ecall", Format::NONE},
    {"fadd.d", Format::R},
    {"fclass.d", Format::R},
    {"fcvt.d.w", Format::R},
    {"fcvt.d.wu", Format::R},
    {"fence", Format::NONE},
    {"fence.tso", Format::NONE},
    {"fld", Format::I},
    {"flw", Format::I},
    {"fmul.d", Format::R},
    {"fmv.d.x", Format::R},
    {"fmv.x.d", Format::R},
    {"fmv.w.x", Format::R},
    {"fsw", Format::S},
    {"fsd", Format::S},
    {"fsgnj.d", Format::R},
    {"fsgnjn.d", Format::R},
    {"fsgnjx.d", Format::R},
    {"fsgnj.s", Format::R},
    {"fsgnjn.s", Format::R},
    {"fsgnjx.s", Format::R},
    {"jal", Format::J},
    {"jalr", Format::I},
    {"lb", Format::I_LOAD},
    {"lbu", Format::I_LOAD},
    {"ld", Format::I_LOAD},
    {"lh", Format::I_LOAD},
    {"lhu", Format::I_LOAD},
    {"lr.d", Format::R_ATOMIC_LR},
    {"lr.w", Format::R_ATOMIC_LR},
    {"lui", Format::U},
    {"lw", Format::I_LOAD},
    {"lwu", Format::I_LOAD},
    {"mul", Format::R},
    {"mulh", Format::R},
    {"mulhu", Format::R},
    {"mulw", Format::R},
    {"or", Format::R},
    {"ori", Format::I},
    {"pause", Format::NONE},
    {"rem", Format::R},
    {"remu", Format::R},
    {"remuw", Format::R},
    {"remw", Format::R},
    {"sb", Format::S},
    {"sc.d", Format::R_ATOMIC},
    {"sc.w", Format::R_ATOMIC},
    {"sd", Format::S},
    {"sh", Format::S},
    {"sll", Format::R},
    {"slli", Format::I_SHIFT},
    {"slliw", Format::I_SHIFT},
    {"sllw", Format::R},
    {"slt", Format::R},
    {"slti", Format::I},
    {"sltiu", Format::I},
    {"sltu", Format::R},
    {"sra", Format::R},
    {"srai", Format::I_SHIFT},
    {"sraiw", Format::I_SHIFT},
    {"sraw", Format::R},
    {"srl", Format::R},
    {"srli", Format::I_SHIFT},
    {"srliw", Format::I_SHIFT},
    {"srlw", Format::R},
    {"sub", Format::R},
    {"subw", Format::R},
    {"sw", Format::S},
    {"xor", Format::R},
    {"xori", Format::I},
});

static_assert(OP_TABLE.size() == NUM_OPS, "len(OP_TABLE) != len(Op::*)");

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

    u64 max_addr = 0;
    for (u64 i = 0; i < ehdr.e_phnum; i++) {
      GElf_Phdr phdr;
      gelf_getphdr(elf, i, &phdr);
      if (phdr.p_type == PT_LOAD) {
        // TODO: probably should disassemble all of those instead of just .text?
        std::copy_n(exe_bytes.data() + phdr.p_offset, phdr.p_filesz,
                    m_memory + phdr.p_vaddr);
        max_addr = std::max(max_addr, phdr.p_vaddr + phdr.p_memsz);
      }
    }
    elf_end(elf);

    m_brk = m_brk_base = (max_addr + 4095ULL) & ~4095ULL; // page align
    m_next_mmap_addr = m_brk_base + 128 * 1024 * 1024;

    u64 num_ins = m_code_section.size / 2;
    m_decoded.resize(num_ins);

    u64 offset = 0;
    while (offset < m_code_section.size) {
      u32 raw;
      std::memcpy(&raw, m_memory + m_code_section.offset + offset, sizeof(raw));

      Ins ins = decode_raw(raw);
      ins.length = ((raw & 0b11) == 0b11) ? 4 : 2;
      m_decoded[offset / 2] = ins;
      disassemble_ins(ins);

      offset += ins.length;
    }
  }

  ~RISCV64() { munmap(m_memory, MEMORY_SIZE); }

  void disassemble_all() {
    m_pc = m_code_section.offset;
    while (m_pc < m_code_section.offset + m_code_section.size) {
      Ins ins = m_decoded[(m_pc - m_code_section.offset) / 2];
      disassemble_ins(ins);
      m_pc += ins.length;
    }
  }

  // TODO: the registers in floating-point instructions should use the f0-f31
  // registers but that would require rewriting a lot of stuff and its not gonna
  // be a problem in execution so we'll live with that for now
  void disassemble_ins(Ins ins) {
    assert((u64)ins.op < OP_TABLE.size());

    const OpDef &def = OP_TABLE[ins.op];

    switch (def.format) {
    case Format::NONE:
      std::println("{}", def.mnemonic);
      break;
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
    case Format::CB:
      std::println("{} {}, {}", def.mnemonic, REGS[ins.rs1], ins.imm);
      break;
    case Format::J:
      std::println("{} {}, {}", def.mnemonic, REGS[ins.rd], ins.imm);
      break;
    case Format::CI:
      std::println("{} {}, {}", def.mnemonic, REGS[ins.rd], ins.imm);
      break;
    case Format::CJ:
      std::println("{} {}", def.mnemonic, ins.imm);
      break;
    case Format::CSS:
      std::println("{} {}, {}(sp)", def.mnemonic, REGS[ins.rs2], ins.imm);
      break;
    case Format::CR:
      std::println("{} {}, {}", def.mnemonic, REGS[ins.rd], REGS[ins.rs2]);
      break;
    case Format::CR1:
      std::println("{} {}", def.mnemonic, REGS[ins.rs1]);
      break;
    case Format::CL:
      std::println("{} {}, {}({})", def.mnemonic, REGS[ins.rd], ins.imm,
                   REGS[ins.rs1]);
      break;
    case Format::R_ATOMIC_LR:
      std::println("{} {}, ({})", def.mnemonic, REGS[ins.rd], REGS[ins.rs1]);
      break;
    case Format::R_ATOMIC:
      std::println("{} {}, {}, ({})", def.mnemonic, REGS[ins.rd], REGS[ins.rs2],
                   REGS[ins.rs1]);
      break;
    case Format::CSR:
      std::println("{} {}, {}, {}", def.mnemonic, REGS[ins.rd], ins.imm,
                   REGS[ins.rs1]);
      break;
    case Format::CSRI:
      std::println("{} {}, {}, {}", def.mnemonic, REGS[ins.rd], ins.imm,
                   ins.rs1);
      break;
    }
  }

  void dump() {
    for (u64 i = 0; i < 32; i++) {
      std::print("{}=0x{:x} ", REGS[i], m_regs[i]);
    }
    std::println();
  }

  void push_u64(u64 v) {
    m_regs[2] -= 8;
    mem_write<u64>(m_regs[2], v);
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

      Ins i = m_decoded[(m_pc - m_code_section.offset) / 2];

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
        m_regs[i.rd] = m_pc + (i64)(i32)((u32)i.imm << 12);
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
        if ((u64)m_regs[i.rs1] >= (u64)m_regs[i.rs2]) {
          m_pc += i.imm;
          continue;
        }
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
      case Op::C_ADD: {
        m_regs[i.rd] += m_regs[i.rs2];
      }; break;
      case Op::C_ADDI: {
        m_regs[i.rd] += i.imm;
      }; break;
      case Op::C_ADDIW: {
        m_regs[i.rd] = (i64)(i32)m_regs[i.rd] + i.imm;
      }; break;
      case Op::C_ADDI16SP: {
        sp += i.imm;
      }; break;
      case Op::C_ADDI4SPN: {
        m_regs[i.rd] = sp + i.imm;
      }; break;
      case Op::C_ADDW: {
        m_regs[i.rd] = (i64)((i32)m_regs[i.rd] + (i32)m_regs[i.rs2]);
      }; break;
      case Op::C_AND: {
        m_regs[i.rd] &= m_regs[i.rs2];
      }; break;
      case Op::C_ANDI: {
        m_regs[i.rd] = m_regs[i.rs1] & i.imm;
      }; break;
      case Op::C_BEQZ: {
        if (m_regs[i.rs1] == 0) {
          m_pc += i.imm;
          continue;
        }
      }; break;
      case Op::C_BNEZ: {
        if (m_regs[i.rs1] != 0) {
          m_pc += i.imm;
          continue;
        }
      }; break;
      case Op::C_EBREAK: {
        std::println(stderr, "EBREAK at pc=0x{:x}", m_pc);
        dump();
        exit(1);
      }; break;
      case Op::C_J: {
        m_pc += i.imm;
        continue;
      }; break;
      case Op::C_JALR: {
        m_regs[1] = m_pc + 2;
        m_pc = m_regs[i.rs1] & ~1ULL;
        continue;
      }; break;
      case Op::C_JR: {
        m_pc = m_regs[i.rs1] & ~1ULL;
        continue;
      }; break;
      case Op::C_LD: {
        u64 addr = m_regs[i.rs1] + i.imm;
        m_regs[i.rd] = mem_read<u64>(addr);
      }; break;
      case Op::C_LDSP: {
        u64 addr = sp + i.imm;
        m_regs[i.rd] = mem_read<u64>(addr);
      }; break;
      case Op::C_LI: {
        m_regs[i.rd] = i.imm;
      }; break;
      case Op::C_LUI: {
        m_regs[i.rd] = (i64)(i32)((u32)i.imm << 12);
      }; break;
      case Op::C_LW: {
        u64 addr = m_regs[i.rs1] + i.imm;
        m_regs[i.rd] = (i64)mem_read<i32>(addr);
      }; break;
      case Op::C_MV: {
        m_regs[i.rd] = m_regs[i.rs2];
      }; break;
      case Op::C_OR: {
        m_regs[i.rd] |= m_regs[i.rs2];
      }; break;
      case Op::C_SDSP: {
        u64 addr = sp + i.imm;
        mem_write<u64>(addr, m_regs[i.rs2]);
      }; break;
      case Op::C_SLLI: {
        m_regs[i.rd] = (i64)((u64)m_regs[i.rd] << i.imm);
      }; break;
      case Op::C_SRAI: {
        m_regs[i.rd] >>= i.imm;
      }; break;
      case Op::C_SRLI: {
        m_regs[i.rd] = (i64)((u64)m_regs[i.rd] >> i.imm);
      }; break;
      case Op::C_SUB: {
        m_regs[i.rd] -= m_regs[i.rs2];
      }; break;
      case Op::C_SUBW: {
        m_regs[i.rd] = (i32)(m_regs[i.rd] - m_regs[i.rs2]);
      }; break;
      case Op::C_SWSP: {
        u64 addr = (u64)sp + (u64)i.imm;
        mem_write<u32>(addr, m_regs[i.rs2]);
      }; break;
      case Op::C_LWSP: {
        u64 addr = (u64)sp + (u64)i.imm;
        m_regs[i.rd] = (i32)mem_read<u32>(addr);
      }; break;
      case Op::C_XOR: {
        m_regs[i.rd] ^= m_regs[i.rs2];
      }; break;
      case Op::DIV: {
        if (m_regs[i.rs2] == 0) {
          m_regs[i.rd] = -1;
        } else {
          m_regs[i.rd] = m_regs[i.rs1] / m_regs[i.rs2];
        }
      }; break;
      case Op::DIVU: {
        if ((u64)m_regs[i.rs2] == 0) {
          m_regs[i.rd] = -1;
        } else {
          m_regs[i.rd] = (i64)((u64)m_regs[i.rs1] / (u64)m_regs[i.rs2]);
        }
      }; break;
      case Op::DIVUW: {
        if ((u32)m_regs[i.rs2] == 0) {
          m_regs[i.rd] = -1;
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
        // ^ already got 2 syscalls wrong
        switch (m_regs[17]) {
        case 29: { // ioctl
          u32 fd = m_regs[10];
          u32 cmd = m_regs[11];
          u64 arg = m_regs[12];

          switch (cmd) {
          case 0x5413: { // TIOCGWINSZ
            m_regs[10] = -ENOTTY;
          }; break;
          default: {
            std::println(stderr, "ioctl(fd={}, cmd={}, arg={}) unimplemented",
                         fd, cmd, arg);
            exit(1);
          }; break;
          }
        }; break;
        case 62: { // lseek
          u32 fd = m_regs[10];
          // i64 offset = m_regs[11];
          // u32 whence = m_regs[12];

          if (fd != 0) {
            std::println(stderr, "lseek syscall implemented only for stdin");
            exit(1);
          }

          // TODO
          m_regs[10] = -ESPIPE;
        }; break;
        case 63: { // read
          u32 fd = m_regs[10];
          u64 buf = m_regs[11];
          u64 count = m_regs[12];

          if (fd != 0) {
            std::println(stderr, "read syscall implemented only for stdin");
            exit(1);
          }

          u64 bytes_read = 0;
          for (u64 i = 0; i < count; i++) {
            char c;
            if (!std::cin.get(c))
              break;
            m_memory[buf + i] = (u8)c;
            bytes_read++;
            if (c == '\n')
              break;
          }

          m_regs[10] = bytes_read;
        }; break;
        case 64: { // write
          u32 fd = m_regs[10];
          u64 buf = m_regs[11];
          u64 count = m_regs[12];

          if (fd != 1) {
            std::println(stderr, "write syscall implemented only for stdout.");
            exit(1);
          }

          u64 end = buf + count;
          for (u64 i = buf; i < end; i++) {
            std::cout.put(m_memory[i]);
          }

          m_regs[10] = count;
        }; break;
        case 66: { // writev
          u32 fd = m_regs[10];
          u64 vec = m_regs[11];
          u64 vlen = m_regs[12];

          if (fd != 1) {
            std::println(stderr, "writev syscall implemented only for stdout.");
            exit(1);
          }

          u64 total_written = 0;
          for (u64 i = 0; i < vlen; i++) {
            u64 iov_entry = vec + i * 16;
            u64 buf = mem_read<u64>(iov_entry);
            u64 len = mem_read<u64>(iov_entry + 8);

            for (u64 j = 0; j < len; j++) {
              std::cout.put(m_memory[buf + j]);
            }
            total_written += len;
          }

          m_regs[10] = total_written;
        } break;

        case 93:   // exit
        case 94: { // exit_group
          std::println("Program exited with code {}.", m_regs[10]);
          return;
        }; break;
        case 96: { // set_tid_address
          i64 tidptr = m_regs[10];
          i32 tid = 123;
          memcpy(&m_memory[tidptr], &tid, sizeof(tid));
          m_regs[10] = tid;
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
        case 214: { // brk
          u64 brk = m_regs[10];

          if (brk >= m_brk_base) {
            m_brk = brk;
          }
          m_regs[10] = (i64)m_brk;
        }; break;
        case 222: { // mmap
          u64 addr = m_regs[10];
          u64 length = m_regs[11];
          // i32 prot = m_regs[12];
          i32 flags = m_regs[13];
          // i32 fd = m_regs[14];
          // i64 offset = m_regs[15];

          if (!(flags & MAP_PRIVATE) || !(flags & MAP_ANONYMOUS)) {
            std::println(
                stderr,
                "mmap implemented only for flags=(MAP_PRIVATE|MAP_ANONYMOUS)",
                flags);
            exit(1);
          }

          if (!(flags & MAP_FIXED)) {
            length = (length + 4095) & ~4095;
            addr = m_next_mmap_addr;
            m_next_mmap_addr += length;
          }

          std::memset(m_memory + addr, 0, length);
          m_regs[10] = addr;
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
        m_regs[i.rd] = mem_read<u64>(m_regs[i.rs1] + i.imm);
      }; break;
      case Op::LH: {
        m_regs[i.rd] = mem_read<i16>(m_regs[i.rs1] + i.imm);
      }; break;
      case Op::LHU: {
        m_regs[i.rd] = mem_read<u16>(m_regs[i.rs1] + i.imm);
      }; break;
      case Op::LUI: {
        m_regs[i.rd] = (i64)(i32)((u32)i.imm << 12);
      }; break;
      case Op::LW: {
        m_regs[i.rd] = mem_read<i32>(m_regs[i.rs1] + i.imm);
      }; break;
      case Op::LWU: {
        m_regs[i.rd] = mem_read<u32>(m_regs[i.rs1] + i.imm);
      }; break;
      case Op::MUL: {
        m_regs[i.rd] = m_regs[i.rs1] * m_regs[i.rs2];
      }; break;
      case Op::MULH: {
        i64 rs1 = m_regs[i.rs1];
        i64 rs2 = m_regs[i.rs2];
        u64 u = rs1;
        u64 v = rs2;
        u64 u0 = u & 0xffffffff;
        u64 u1 = u >> 32;
        u64 v0 = v & 0xffffffff;
        u64 v1 = v >> 32;
        u64 t = u0 * v0;
        u64 k = t >> 32;
        t = u1 * v0 + k;
        u64 w1 = t & 0xffffffff;
        u64 w2 = t >> 32;
        t = u0 * v1 + w1;
        k = t >> 32;
        u64 res = u1 * v1 + w2 + k;
        if (rs1 < 0)
          res -= u64(rs2);
        if (rs2 < 0)
          res -= u64(rs1);
        m_regs[i.rd] = (i64)res;
      }; break;
      case Op::MULHU: {
        u64 a = m_regs[i.rs1];
        u64 b = m_regs[i.rs2];
        u64 a0 = a & 0xffffffff;
        u64 a1 = a >> 32;
        u64 b0 = b & 0xffffffff;
        u64 b1 = b >> 32;
        u64 t = a0 * b0;
        u64 k = t >> 32;
        t = a1 * b0 + k;
        u64 w1 = t & 0xffffffff;
        u64 w2 = t >> 32;
        t = a0 * b1 + w1;
        k = t >> 32;
        m_regs[i.rd] = a1 * b1 + w2 + k;
      }; break;
      case Op::MULW: {
        m_regs[i.rd] = (i32)(m_regs[i.rs1] * m_regs[i.rs2]);
      }; break;
      case Op::OR: {
        m_regs[i.rd] = m_regs[i.rs1] | m_regs[i.rs2];
      }; break;
      case Op::ORI: {
        m_regs[i.rd] = m_regs[i.rs1] | (i64)i.imm;
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
        i32 a = (i32)m_regs[i.rs1];
        i32 b = (i32)m_regs[i.rs2];

        if (b == 0) {
          m_regs[i.rd] = (i64)a;
        } else if (a == INT32_MIN && b == -1) {
          m_regs[i.rd] = 0;
        } else {
          m_regs[i.rd] = (i64)(a % b);
        }
      }; break;
      case Op::SB: {
        u64 addr = m_regs[i.rs1] + i.imm;
        m_memory[addr] = m_regs[i.rs2];
      }; break;
      case Op::SD:
      case Op::C_SD: {
        u64 addr = m_regs[i.rs1] + i.imm;
        mem_write<u64>(addr, m_regs[i.rs2]);
      }; break;
      case Op::SH: {
        u64 addr = m_regs[i.rs1] + i.imm;
        mem_write<u16>(addr, m_regs[i.rs2]);
      }; break;
      case Op::SLL: {
        m_regs[i.rd] = (u64)m_regs[i.rs1] << ((u64)m_regs[i.rs2] & 0b111111);
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
      case Op::SLTIU: {
        m_regs[i.rd] = ((u64)m_regs[i.rs1] < (u64)(i64)i.imm) ? 1 : 0;
      }; break;
      case Op::SLTU: {
        m_regs[i.rd] = ((u64)m_regs[i.rs1] < (u64)m_regs[i.rs2]) ? 1 : 0;
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
      case Op::C_SW:
      case Op::SW: {
        u64 addr = m_regs[i.rs1] + i.imm;
        mem_write<u32>(addr, m_regs[i.rs2]);
      }; break;
      case Op::XOR: {
        m_regs[i.rd] = m_regs[i.rs1] ^ m_regs[i.rs2];
      }; break;
      case Op::XORI: {
        m_regs[i.rd] = m_regs[i.rs1] ^ i.imm;
      }; break;
      default: {
        std::println(stderr, "{} not implemented", OP_TABLE[i.op].mnemonic);
        exit(1);
      }; break;
      }

      m_pc += i.length;
    }
  }

private:
  u8 *m_memory;
  std::vector<Ins> m_decoded;
  u64 m_pc;
  std::array<i64, 32> m_regs{};
  Section m_code_section;
  u64 m_brk;
  u64 m_brk_base;
  u64 m_next_mmap_addr;

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
          std::println(stderr, "Illegal instruction at 0x{:x} (all zeros)",
                       m_pc);
        } else {
          i.rd = ((raw >> 2) & 0b111) + 8;
          i.rs1 = 2;
          i.imm = (((raw >> 11) & 0b11) << 4) | (((raw >> 7) & 0b1111) << 6) |
                  (((raw >> 6) & 0b1) << 2) | (((raw >> 5) & 0b1) << 3);
          i.op = Op::C_ADDI4SPN;
        }
      }; break;
      case 0b001: {
        i.rd = ((raw >> 2) & 0b111) + 8;
        i.rs1 = ((raw >> 7) & 0b111) + 8;
        i.imm = (((raw >> 10) & 0b111) << 3) | (((raw >> 5) & 0b11) << 6);
        i.op = Op::C_FLD;
      }; break;
      case 0b010: {
        i.rd = ((raw >> 2) & 0b111) + 8;
        i.rs1 = ((raw >> 7) & 0b111) + 8;
        i.imm = (((raw >> 10) & 0b111) << 3) | (((raw >> 6) & 0b1) << 2) |
                (((raw >> 5) & 0b1) << 6);
        i.op = Op::C_LW;
      }; break;
      case 0b011: {
        i.rd = ((raw >> 2) & 0b111) + 8;
        i.rs1 = ((raw >> 7) & 0b111) + 8;
        i.imm = (((raw >> 10) & 0b111) << 3) | (((raw >> 5) & 0b11) << 6);
        i.op = Op::C_LD;
      }; break;
      case 0b101: {
        i.rs2 = ((raw >> 2) & 0b111) + 8;
        i.rs1 = ((raw >> 7) & 0b111) + 8;
        i.imm = (((raw >> 10) & 0b111) << 3) | (((raw >> 5) & 0b11) << 6);
        i.op = Op::C_FSD;
      }; break;
      case 0b110: {
        i.rs1 = ((raw >> 7) & 0b111) + 8;
        i.rs2 = ((raw >> 2) & 0b111) + 8;
        i.imm = (((raw >> 10) & 0b111) << 3) | (((raw >> 6) & 0b1) << 2) |
                (((raw >> 5) & 0b1) << 6);
        i.op = Op::C_SW;
      }; break;
      case 0b111: {
        i.rs1 = ((raw >> 7) & 0b111) + 8;
        i.rs2 = ((raw >> 2) & 0b111) + 8;
        i.imm = (((raw >> 10) & 0b111) << 3) | (((raw >> 5) & 0b11) << 6);
        i.op = Op::C_SD;
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
      case 0b001: {
        i.rs1 = i.rd;
        i.imm = ((raw >> 2) & 0b11111) | (((raw >> 12) & 0b1) << 5);
        i.imm = (i.imm << 26) >> 26;
        i.op = Op::C_ADDIW;
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
          i.imm = (((raw >> 12) & 0b1) << 5) | ((raw >> 2) & 0b11111);
          i.imm = (i.imm << 26) >> 26;
          i.op = Op::C_LUI;
        }
      }; break;
      case 0b100: {
        u8 funct2 = (raw >> 10) & 0b11;

        if (funct2 == 0b11) {
          i.rs1 = ((raw >> 7) & 0b111) + 8;
          i.rd = i.rs1;

          bool bit12 = (raw >> 12) & 0b1;
          i.rs2 = ((raw >> 2) & 0b111) + 8;
          u8 sub_op = (raw >> 5) & 0b11;

          if (bit12 == 0) {
            switch (sub_op) {
            case 0b00:
              i.op = Op::C_SUB;
              break;
            case 0b01:
              i.op = Op::C_XOR;
              break;
            case 0b10:
              i.op = Op::C_OR;
              break;
            case 0b11:
              i.op = Op::C_AND;
              break;
            default:
              std::println(
                  stderr,
                  "C: opcode=01: funct3=100: bit12=0: unrecognized sub_op");
              exit(1);
            }
          } else {
            switch (sub_op) {
            case 0b00:
              i.op = Op::C_SUBW;
              break;
            case 0b01:
              i.op = Op::C_ADDW;
              break;
            default:
              std::println(stderr,
                           "C: opcode=01: funct3=100: bit12=1: unrecognized "
                           "sub_op: {:b}",
                           sub_op);
              exit(1);
            }
          }
        } else {
          i.rd = ((raw >> 7) & 0b111) + 8;
          i.rs1 = i.rd;
          i.imm = ((raw >> 2) & 0b11111) | (((raw >> 12) & 0b1) << 5);

          switch (funct2) {
          case 0b00:
            i.op = Op::C_SRLI;
            break;
          case 0b01:
            i.op = Op::C_SRAI;
            break;
          case 0b10:
            i.imm = (i.imm << 26) >> 26;
            i.op = Op::C_ANDI;
            break;
          default:
            std::println(
                stderr,
                "C: opcode=01: funct3=100: funct2!=11: unrecognized funct2");
            exit(1);
          }
        }
      }; break;
      case 0b101: {
        i.rd = 0;
        i.imm = (((raw >> 12) & 0b1) << 11) | (((raw >> 11) & 0b1) << 4) |
                (((raw >> 9) & 0b11) << 8) | (((raw >> 8) & 0b1) << 10) |
                (((raw >> 7) & 0b1) << 6) | (((raw >> 6) & 0b1) << 7) |
                (((raw >> 5) & 0b1) << 3) | (((raw >> 3) & 0b11) << 1) |
                (((raw >> 2) & 0b1) << 5);
        i.imm = (i.imm << 20) >> 20;
        i.op = Op::C_J;
      }; break;
      case 0b110: {
        i.rs1 = ((raw >> 7) & 0b111) + 8;
        i.rs2 = 0;
        i.imm = (((raw >> 12) & 0b1) << 8) | (((raw >> 10) & 0b11) << 3) |
                (((raw >> 5) & 0b11) << 6) | (((raw >> 3) & 0b11) << 1) |
                (((raw >> 2) & 0b1) << 5);
        i.imm = (i.imm << 23) >> 23;
        i.op = Op::C_BEQZ;
      }; break;
      case 0b111: {
        i.rs1 = ((raw >> 7) & 0b111) + 8;
        i.rs2 = 0;
        i.imm = (((raw >> 12) & 0b1) << 8) | (((raw >> 10) & 0b11) << 3) |
                (((raw >> 5) & 0b11) << 6) | (((raw >> 3) & 0b11) << 1) |
                (((raw >> 2) & 0b1) << 5);
        i.imm = (i.imm << 23) >> 23;
        i.op = Op::C_BNEZ;
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
      case 0b001: {
        i.rs1 = 2;
        i.imm = (((raw >> 12) & 0b1) << 5) | (((raw >> 5) & 0b11) << 3) |
                (((raw >> 2) & 0b111) << 6);
        i.op = Op::C_FLDSP;
      }; break;
      case 0b010: {
        i.rs1 = 2;
        i.imm = (((raw >> 2) & 0b11) << 6) | (((raw >> 12) & 0b1) << 5) |
                (((raw >> 4) & 0b111) << 2);
        i.op = Op::C_LWSP;
      }; break;
      case 0b011: {
        i.rs1 = 2;
        i.imm = (((raw >> 12) & 0b1) << 5) | (((raw >> 5) & 0b11) << 3) |
                (((raw >> 2) & 0b111) << 6);
        i.op = Op::C_LDSP;
      }; break;
      case 0b100: {
        bool bit12 = (raw >> 12) & 0b1;
        i.rs2 = (raw >> 2) & 0b11111;

        if (bit12 == 0) {
          if (i.rs2 == 0) {
            i.rs1 = i.rd;
            i.rd = 0;
            i.imm = 0;
            i.op = Op::C_JR;
          } else {
            i.rs1 = 0;
            i.op = Op::C_MV;
          }
        } else {
          if (i.rs2 == 0) {
            if (i.rd == 0) {
              i.op = Op::C_EBREAK;
            } else {
              i.rs1 = i.rd;
              i.rd = 1;
              i.imm = 0;
              i.op = Op::C_JALR;
            }
          } else {
            i.rs1 = i.rd;
            i.op = Op::C_ADD;
          }
        }
      }; break;
      case 0b101: {
        i.rs2 = (raw >> 2) & 0b11111;
        i.rs1 = 2;
        i.imm = (((raw >> 10) & 0b111) << 3) | (((raw >> 7) & 0b111) << 6);
        i.op = Op::C_FSDSP;
      }; break;
      case 0b110: {
        i.rs2 = (raw >> 2) & 0b11111;
        i.rs1 = 2;
        i.imm = (((raw >> 9) & 0b1111) << 2) | (((raw >> 7) & 0b11) << 6);
        i.op = Op::C_SWSP;
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
          std::println(stderr, "0010011: funct3=001: unrecognized funct6: {:b}",
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
          std::println(stderr, "0010011: funct3=101: unrecognized funct6: {:b}",
                       funct6);
          exit(1);
        }
      } else if (funct3 == 0b110) {
        i.op = Op::ORI;
      } else if (funct3 == 0b111) {
        i.op = Op::ANDI;
      } else {
        std::println(stderr, "0010011: unrecognized funct3: {:03b}", funct3);
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
        std::println(stderr, "0000011: unrecognized funct3: {:03b}", funct3);
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
      i.imm = (raw >> 20) & 0b111111111111;

      switch (funct3) {
      case 0b000: {
        if (i.imm == 0) {
          i.op = Op::ECALL;
        } else {
          std::println(stderr, "1110011: funct3=000: unrecognized imm: {:b}",
                       i.imm);
          exit(1);
        }
      }; break;
      case 0b010: {
        i.op = Op::CSRRS;
      }; break;
      case 0b110: {
        i.op = Op::CSRRSI;
      }; break;
      default:
        std::println(stderr, "1110011: unrecognized funct3: {:03b}", funct3);
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
    case 0b0101111: {
      u8 funct3 = (raw >> 12) & 0b111;
      u8 funct7 = (raw >> 27) & 0b11111;
      i.rd = (raw >> 7) & 0b11111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.rs2 = (raw >> 20) & 0b11111;

      if (funct3 == 0b010) {
        switch (funct7) {
        case 0b00000: {
          i.op = Op::AMOADD_W;
        }; break;
        case 0b00001: {
          i.op = Op::AMOSWAP_W;
        }; break;
        case 0b00010: {
          i.op = Op::LR_W;
        }; break;
        case 0b00011: {
          i.op = Op::SC_W;
        }; break;
        case 0b01000: {
          i.op = Op::AMOOR_W;
        }; break;
        case 0b11100: {
          i.op = Op::AMOMAXU_W;
        }; break;
        default: {
          std::println(stderr,
                       "0101111: funct3=010: unrecognized funct7: {:05b}",
                       funct7);
          exit(1);
        }; break;
        }
      } else if (funct3 == 0b011) {
        switch (funct7) {
        case 0b00000: {
          i.op = Op::AMOADD_D;
        }; break;
        case 0b00001: {
          i.op = Op::AMOSWAP_D;
        }; break;
        case 0b00010: {
          i.op = Op::LR_D;
        }; break;
        case 0b00011: {
          i.op = Op::SC_D;
        }; break;
        case 0b11100: {
          i.op = Op::AMOMAXU_D;
        }; break;
        default: {
          std::println(stderr,
                       "0101111: funct3=011: unrecognized funct7: {:05b}",
                       funct7);
          exit(1);
        }; break;
        }
      } else {
        std::println(stderr, "0101111: unrecognized funct3: {:03b}", funct3);
        exit(1);
      }
    }; break;
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
      u8 funct3 = (raw >> 12) & 0b111;

      switch (funct3) {
      case 0b010: {
        i.rd = (raw >> 7) & 0b11111;
        i.rs1 = (raw >> 15) & 0b11111;
        i.imm = (i32)raw >> 20;
        i.op = Op::FLW;
      }; break;
      case 0b011: {
        i.rd = (raw >> 7) & 0b11111;
        i.rs1 = (raw >> 15) & 0b11111;
        i.imm = (i32)raw >> 20;
        i.op = Op::FLD;
      }; break;
      default:
        std::println(stderr, "0000111: unrecognized funct3: {:03b}", funct3);
        exit(1);
      }
    }; break;
    case 0b1010011: {
      u8 funct3 = (raw >> 12) & 0b111;
      u8 funct7 = (raw >> 25) & 0b1111111;
      i.rs1 = (raw >> 15) & 0b11111;
      i.rs2 = (raw >> 20) & 0b11111;

      switch (funct7) {
      case 0b0000001: {
        i.op = Op::FADD_D;
      }; break;
      case 0b0010000: {
        switch (funct3) {
        case 0b000:
          i.op = Op::FSGNJ_S;
          break;
        case 0b001:
          i.op = Op::FSGNJN_S;
          break;
        case 0b010:
          i.op = Op::FSGNJX_S;
          break;
        default:
          std::println(stderr,
                       "1010011: funct7=0010000: unrecognized funct3: {:03b}",
                       funct3);
          exit(1);
        }
      }; break;
      case 0b1111001: {
        i.rs1 = (raw >> 15) & 0b11111;
        i.op = Op::FMV_D_X;
      }; break;
      case 0b1101001: {
        if (i.rs2 == 0b00000) {
          i.op = Op::FCVT_D_W;
        } else if (i.rs2 == 0b00001) {
          i.op = Op::FCVT_D_WU;
        } else {
          std::println(
              stderr, "1010011: funct7=1101001: unrecognized rs2: {:b}", i.rs2);
          exit(1);
        }
      }; break;
      case 0b0001001: {
        i.op = Op::FMUL_D;
      }; break;
      case 0b0010001: {
        switch (funct3) {
        case 0b000:
          i.op = Op::FSGNJ_D;
          break;
        case 0b001:
          i.op = Op::FSGNJN_D;
          break;
        case 0b010:
          i.op = Op::FSGNJX_D;
          break;
        default:
          std::println(stderr,
                       "1010011: funct7=0010001: unrecognized funct3: {:03b}",
                       funct3);
          exit(1);
        }
      }; break;
      case 0b1110001: {
        switch (funct3) {
        case 0b000: {
          i.op = Op::FMV_X_D;
        }; break;
        case 0b001: {
          i.op = Op::FCLASS_D;
        }; break;
        default: {
          std::println(stderr,
                       "1010011: funct7=1110001: unrecognized funct3: {:03b}",
                       funct3);
          exit(1);
        }; break;
        }
      }; break;
      case 0b1111000: {
        i.op = Op::FMV_W_X;
      }; break;
      default: {
        std::println(stderr, "1010011: unrecognized funct7: {:07b}", funct7);
        exit(1);
      }; break;
      }
    }; break;
    case 0b0100111: {
      u8 funct3 = (raw >> 12) & 0b111;

      switch (funct3) {
      case 0b010: {
        i.rs1 = (raw >> 15) & 0b11111;
        i.rs2 = (raw >> 20) & 0b11111;
        i32 imm_hi = (i32)raw >> 25;
        i32 imm_lo = (raw >> 7) & 0b11111;
        i.imm = (imm_hi << 5) | imm_lo;
        i.op = Op::FSW;
      }; break;
      case 0b011: {
        i.rs1 = (raw >> 15) & 0b11111;
        i.rs2 = (raw >> 20) & 0b11111;
        i32 imm_hi = (i32)raw >> 25;
        i32 imm_lo = (raw >> 7) & 0b11111;
        i.imm = (imm_hi << 5) | imm_lo;
        i.op = Op::FSD;
      }; break;
      default: {
        std::println(stderr, "0100111: unrecognized funct3: {:03b}", funct3);
        exit(1);
      }; break;
      }
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
        std::println(stderr, "0100011: unrecognized funct3: {:03b}", funct3);
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
    case 0b0001111: {
      u8 funct3 = (raw >> 12) & 0b111;
      if (funct3 == 0b000) {
        i.imm = (raw >> 20) & 0b111111111111;

        if (i.imm == 0b000000010000) {
          i.op = Op::PAUSE;
        } else if (i.imm == 0b100000110011) {
          i.op = Op::FENCE_TSO;
        } else {
          i.op = Op::FENCE;
        }
      } else {
        std::println(stderr, "0001111: unrecognized funct3: {:03b}", funct3);
        exit(1);
      }
    }; break;
    default:
      std::println(stderr, "Unrecognized opcode: {:07b}", opcode);
      exit(1);
    }

    return i;
  }

  template <typename T> T mem_read(u64 addr) {
    T v;
    std::memcpy(&v, m_memory + addr, sizeof(T));
    return v;
  }
  template <typename T> void mem_write(u64 addr, T v) {
    std::memcpy(m_memory + addr, &v, sizeof(T));
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

  std::println("END DISASSEMBLY");

  r.execute();
}
