// https://docs.riscv.org/reference/isa/_attachments/riscv-unprivileged.pdf
// https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf
// https://jborza.com/post/2021-05-11-riscv-linux-syscalls/
#include <gelf.h>
#include <libelf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  size_t offset;
  size_t size;
} Section;

Section get_code_section(uint8_t *exe_bytes, size_t exe_size) {
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

  Elf_Scn *section = NULL;
  while ((section = elf_nextscn(elf, section)) != NULL) {
    GElf_Shdr header;
    if (gelf_getshdr(section, &header) != &header)
      continue;

    const char *name = elf_strptr(elf, str_table_index, header.sh_name);
    if (name && strcmp(name, ".text") == 0) {
      elf_end(elf);
      return (Section){.offset = header.sh_offset, .size = header.sh_size};
    }
  }

  fprintf(stderr, "Failed to locate .text\n");
  exit(1);
}

void riscv64_disassemble(uint8_t *exe_bytes, Section section) {
  size_t offset = section.offset;

  while (offset < section.offset + section.size) {
    uint32_t ins;
    memcpy(&ins, exe_bytes + offset, 4);

    uint16_t opcode = ins & 0x7f; // last 7 bits

    // https://stackoverflow.com/questions/62939410/how-can-i-find-out-the-instruction-format-of-a-risc-v-instruction
    switch (opcode) {
    case 0b1100011:
      printf("B-type\n");
      break;
    case 0b0010011:
      printf("I-type 1\n");
      break;
    case 0b0000011:
      printf("I-type 2\n");
      break;
    case 0b1100111:
      printf("I-type 3\n");
      break;
    case 0b1110011:
      printf("I-type 4\n");
      break;
    case 0b1101111:
      printf("J-type\n");
      break;
    case 0b0110011:
      printf("R-type 1\n");
      break;
    case 0b0101111:
      printf("R-type 2\n");
      break;
    case 0b0111011:
      printf("R-type 3\n");
      break;
    case 0b0100011:
      printf("S-type\n");
      break;
    case 0b0110111:
      printf("U-type 1\n");
      break;
    case 0b0010111:
      printf("U-type 2\n");
      break;
    default:
      fprintf(stderr, "Unrecognized opcode: %07b\n", opcode);
      exit(1);
    }

    offset += 4;
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

  Section section = get_code_section(exe_bytes, exe_size);

  riscv64_disassemble(exe_bytes, section);
}
