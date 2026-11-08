//type:rp
//options:--gcc:--gcc -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* __attribute__((used)): a static variable and a static function that nothing
   refers to stay in the object file (the program reads its own symbol table
   from /proc/self/exe).  Without the attribute the optimizer would drop
   them. */
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int kept_variable __attribute__((used)) = 41;
static const char kept_string[] __attribute__((used)) = "kept";
static int kept_function(void) __attribute__((used));
static int kept_function(void)
{
  return 7;
}

/* The binding of the symbol NAME in the program's own symbol table: STB_LOCAL
   (0), STB_GLOBAL (1) or STB_WEAK (2), or -1 when there is no such symbol. */
static int symbol_binding(const char *name)
{
  FILE *file = fopen("/proc/self/exe", "rb");
  unsigned char *image;
  long size;
  Elf64_Ehdr *header;
  Elf64_Shdr *sections;
  int result = -1;
  int index;

  if (file == NULL)
    return -2;
  fseek(file, 0, SEEK_END);
  size = ftell(file);
  fseek(file, 0, SEEK_SET);
  image = malloc((size_t)size);
  if (image == NULL || fread(image, 1, (size_t)size, file) != (size_t)size) {
    fclose(file);
    return -3;
  }
  fclose(file);
  header = (Elf64_Ehdr *)image;
  sections = (Elf64_Shdr *)(image + header->e_shoff);
  for (index = 0; index < header->e_shnum; index++) {
    if (sections[index].sh_type == SHT_SYMTAB) {
      Elf64_Sym *symbols = (Elf64_Sym *)(image + sections[index].sh_offset);
      size_t count = sections[index].sh_size / sizeof(Elf64_Sym);
      const char *strings =
        (const char *)(image + sections[sections[index].sh_link].sh_offset);
      size_t entry;

      for (entry = 0; entry < count; entry++)
        if (strcmp(strings + symbols[entry].st_name, name) == 0)
          result = ELF64_ST_BIND(symbols[entry].st_info);
    }
  }
  free(image);
  return result;
}

int main(void)
{
  int failures = 0;

  if (symbol_binding("kept_variable") != STB_LOCAL)
    failures |= 1;
  if (symbol_binding("kept_string") != STB_LOCAL)
    failures |= 2;
  if (symbol_binding("kept_function") != STB_LOCAL)
    failures |= 4;
  if (symbol_binding("main") != STB_GLOBAL)
    failures |= 8;                      /* the reader itself works */
  return failures;
}
