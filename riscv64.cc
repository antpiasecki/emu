// https://docs.riscv.org/reference/isa/unpriv/rv-32-64g.html
// https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf

#include <array>
#include <cstring>
#include <fstream>
#include <gelf.h>
#include <iostream>
#include <libelf.h>
#include <vector>

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
static_assert(sizeof(size_t) == sizeof(u64), "size_t must be 64-bit");

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
        memcpy(m_memory.data() + phdr.p_vaddr, exe_bytes.data() + phdr.p_offset,
               phdr.p_filesz);
      }
    }
    elf_end(elf);
  }

private:
  std::vector<u8> m_memory = std::vector<u8>(MEMORY_SIZE, 0);
  u64 m_pc;
  std::array<i64, 32> m_regs = {0};
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

  exe_bytes = {};

  // r.disassemble_all();
  // printf("END DISASSEMBLY\n");

  // r.execute();
}
