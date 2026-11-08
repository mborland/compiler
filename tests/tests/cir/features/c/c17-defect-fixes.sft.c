//type:rp
//options:--c17:--c17 -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* C17 (ISO/IEC 9899:2018) has no new language feature: __STDC_VERSION__ is
   201710L and the C11 features keep working.  DR 481 (C17): the controlling
   expression of _Generic is converted as an lvalue, so qualifiers drop. */
#include <stddef.h>

#if __STDC_VERSION__ != 201710L
#error "not C17"
#endif

struct point { int x, y; };
struct tagged {
  int kind;
  union { int i; float f; };            /* C11 anonymous union */
};

_Static_assert(sizeof(int) == 4, "int is 32 bits");

#define KIND(x) _Generic((x), int: 1, long: 2, double: 3, char *: 4, default: 0)

static _Noreturn void never(void)
{
  for (;;)
    ;
}

int main(void)
{
  const int ci = 5;
  struct tagged t = { 1, { .i = 7 } };
  _Alignas(16) char aligned[16];
  int failures = 0;

  if (KIND(ci) != 1)                    /* const int selects int */
    failures |= 1;
  if (KIND(3.0) != 3 || KIND(1L) != 2 || KIND("x" + 0) != 4)
    failures |= 2;
  if (t.i != 7)
    failures |= 4;
  if (_Alignof(struct point) != 4 || ((size_t)aligned & 15) != 0)
    failures |= 8;
  for (int i = 0; i < 3; i++)
    failures += 0 * i;
  if (0)
    never();
  return failures;
}
