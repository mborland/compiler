#include "p.h"
extern "C" void *__real___cxa_allocate_exception(unsigned long);
extern "C" void __real___cxa_free_exception(void *);
static int allocs, frees;
extern "C" void *__wrap___cxa_allocate_exception(unsigned long n) { ++allocs; return __real___cxa_allocate_exception(n); }
extern "C" void __wrap___cxa_free_exception(void *p) { ++frees; __real___cxa_free_exception(p); }
int main() {
  int s = run_all();
  printf("run_all %d allocs %d frees %d\n", s, allocs, frees);
  return 0;
}
