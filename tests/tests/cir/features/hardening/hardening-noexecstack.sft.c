//type:rp
//options:--gcc:--gcc -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* Non-executable stack: the object file that the CIR back end writes carries
   the .note.GNU-stack section, so the linked program has a PT_GNU_STACK
   header without the execute flag and its stack mapping is not executable.
   The program reads its own program headers (/proc/self/exe) and its own
   mappings (/proc/self/maps). */
#include <elf.h>
#include <stdio.h>
#include <string.h>

static int stack_header_flags(void)
{
  FILE *file = fopen("/proc/self/exe", "rb");
  Elf64_Ehdr header;
  Elf64_Phdr entry;
  int index;
  int flags = -1;

  if (file == NULL)
    return -2;
  if (fread(&header, sizeof header, 1, file) != 1) {
    fclose(file);
    return -3;
  }
  for (index = 0; index < header.e_phnum; index++) {
    fseek(file, (long)(header.e_phoff + (Elf64_Off)index * header.e_phentsize),
          SEEK_SET);
    if (fread(&entry, sizeof entry, 1, file) != 1)
      break;
    if (entry.p_type == PT_GNU_STACK)
      flags = (int)entry.p_flags;
  }
  fclose(file);
  return flags;
}

/* The permission string of the [stack] mapping ("rw-p"), or "" if none. */
static void stack_mapping(char *perms)
{
  FILE *file = fopen("/proc/self/maps", "r");
  char line[512];

  perms[0] = '\0';
  if (file == NULL)
    return;
  while (fgets(line, sizeof line, file) != NULL) {
    if (strstr(line, "[stack]") != NULL) {
      char *space = strchr(line, ' ');

      if (space != NULL) {
        memcpy(perms, space + 1, 4);
        perms[4] = '\0';
      }
    }
  }
  fclose(file);
}

int main(void)
{
  char perms[8];
  int flags = stack_header_flags();
  int failures = 0;

  if (flags < 0)
    failures |= 1;                      /* no PT_GNU_STACK: executable stack */
  else if (flags & PF_X)
    failures |= 2;
  if ((flags & PF_R) == 0 || (flags & PF_W) == 0)
    failures |= 4;
  stack_mapping(perms);
  if (perms[0] == '\0' || perms[2] == 'x')
    failures |= 8;
  return failures;
}
