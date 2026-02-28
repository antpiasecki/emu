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

  printf("Offset: %zu\n", section.offset);
  printf("Size: %zu\n", section.size);
}
