// https://docs.riscv.org/reference/isa/unpriv/rv-32-64g.html
// https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf

#if !defined(__cplusplus) || __cplusplus < 201703L
#error "This file requires at least C++17"
#endif

#include <array>
#include <fstream>
#include <gelf.h>
#include <iostream>
#include <libelf.h>
#include <vector>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

static constexpr u64 MEMORY_SIZE = 20 * 1024 * 1024; // should be enough

static std::array<const char *, 32> REGS = {
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
  RISCV64(const std::vector<char> &exe_bytes) {}

private:
  std::array<u8, MEMORY_SIZE> m_memory;
  u64 m_pc;
  i64 m_regs[32];
  Section m_code_section;
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

  exe_bytes = {};

  r.disassemble_all();
  std::cout << "END DISASSEMBLY\n";

  // for (r.pc = r.code_section.offset;
  //      r.pc < r.code_section.offset + r.code_section.size; r.pc += 4) {
  //   riscv64_disassemble_one(&r);
  // }
  // printf("END DISASSEMBLY\n");

  // riscv64_execute(&r);
}
