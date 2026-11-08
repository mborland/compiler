//type:rp
//options:--gcc:--gcc -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* __attribute__((weak)): weak definitions have weak binding in the symbol
   table (the program reads its own, from /proc/self/exe), and a weak
   undefined function or variable has the address 0, which the optimizer
   must not fold away. */
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int weak_variable __attribute__((weak)) = 3;
int weak_function(void) __attribute__((weak));
int weak_function(void)
{
  return 4;
}
extern int missing_function(void) __attribute__((weak));
extern int missing_variable __attribute__((weak));
extern void weakref_declared(void) __attribute__((weak));

/* The binding of the symbol NAME (0 local, 1 global, 2 weak), or -1 when
   there is no such symbol; *UNDEFINED is set when its section index is
   SHN_UNDEF. */
static int symbol_binding(const char *name, int *undefined)
{
  FILE *file = fopen("/proc/self/exe", "rb");
  unsigned char *image;
  long size;
  Elf64_Ehdr *header;
  Elf64_Shdr *sections;
  int result = -1;
  int index;

  *undefined = 0;
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
        if (strcmp(strings + symbols[entry].st_name, name) == 0) {
          result = ELF64_ST_BIND(symbols[entry].st_info);
          *undefined = symbols[entry].st_shndx == SHN_UNDEF;
        }
    }
  }
  free(image);
  return result;
}

int main(void)
{
  int failures = 0;
  int undefined;

  if (weak_variable != 3 || weak_function() != 4)
    failures |= 1;
  if (missing_function != 0 || &missing_variable != 0 || weakref_declared)
    failures |= 2;                      /* absent: the address is 0 */
  if (symbol_binding("weak_variable", &undefined) != STB_WEAK || undefined)
    failures |= 4;
  if (symbol_binding("weak_function", &undefined) != STB_WEAK || undefined)
    failures |= 8;
  /* An unreferenced weak undefined symbol may be absent from the table; a
     referenced one is weak and undefined. */
  if (symbol_binding("missing_function", &undefined) != STB_WEAK || !undefined)
    failures |= 16;
  return failures;
}
