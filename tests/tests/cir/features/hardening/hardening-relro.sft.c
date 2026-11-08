//type:rp
//options:--gcc:--gcc -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* RELRO: the linker (the gcc driver, with its defaults) makes the relocated
   part of the data read-only once the program is loaded.  The program reads
   its own program headers (/proc/self/exe): it needs a PT_GNU_RELRO header,
   and the mapping that holds a relocated pointer must not be writable in
   /proc/self/maps.  BIND_NOW is not tested: it is not the default of the
   cir-env gcc, and eccp rejects -Wl, options (hardening-bind-now-negative). */
#include <elf.h>
#include <stdint.h>
#include <stdio.h>

/* A pointer that the dynamic linker relocates (position independent code). */
static int target;
int *const relocated_pointer = &target;

static uintptr_t address_of(const void *pointer)
{
  return (uintptr_t)pointer;
}

int main(void)
{
  FILE *file = fopen("/proc/self/exe", "rb");
  Elf64_Ehdr header;
  Elf64_Phdr entry;
  int index;
  int have_relro = 0;
  int failures = 0;

  if (file == NULL)
    return 100;
  if (fread(&header, sizeof header, 1, file) != 1)
    return 101;
  for (index = 0; index < header.e_phnum; index++) {
    fseek(file, (long)(header.e_phoff + (Elf64_Off)index * header.e_phentsize),
          SEEK_SET);
    if (fread(&entry, sizeof entry, 1, file) != 1)
      return 102;
    if (entry.p_type == PT_GNU_RELRO)
      have_relro = 1;
  }
  fclose(file);
  if (!have_relro)
    failures |= 1;
  {
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[512];
    uintptr_t address = address_of(&relocated_pointer);
    int found = 0;

    if (maps == NULL)
      return 103;
    while (fgets(line, sizeof line, maps) != NULL) {
      unsigned long low, high;
      char perms[8];

      if (sscanf(line, "%lx-%lx %7s", &low, &high, perms) == 3 &&
          address >= low && address < high) {
        found = 1;
        if (perms[1] == 'w')
          failures |= 2;                /* the region is still writable */
      }
    }
    fclose(maps);
    if (!found)
      failures |= 4;
  }
  return failures;
}
