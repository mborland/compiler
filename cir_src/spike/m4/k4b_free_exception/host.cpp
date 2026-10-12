#include "p.h"
extern "C" {
void *__real___cxa_allocate_exception(unsigned long);
void __real___cxa_free_exception(void *);
static int allocs = 0, frees = 0;
void *__wrap___cxa_allocate_exception(unsigned long n) { ++allocs; return __real___cxa_allocate_exception(n); }
void __wrap___cxa_free_exception(void *p) { ++frees; __real___cxa_free_exception(p); }
}
int main() {
  int s = 0;
  for (int k = 0; k < 5; ++k) s += f1(k) + f2(k) + f3(k);
  printf("sum %d alloc %d free %d\n", s, allocs, frees);
  return 0;
}
