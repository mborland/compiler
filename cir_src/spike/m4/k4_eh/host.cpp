#include "p.h"
extern "C" {
void *__real___cxa_allocate_exception(unsigned long);
void __real___cxa_free_exception(void *);
static int allocs = 0, frees = 0;
void *__wrap___cxa_allocate_exception(unsigned long n) { ++allocs; return __real___cxa_allocate_exception(n); }
void __wrap___cxa_free_exception(void *p) { ++frees; __real___cxa_free_exception(p); }
}
int main() {
  int s = run_all();
  printf("sum %d\n", s);
  printf("alloc %d free %d\n", allocs, frees);   // free-on-throw: frees of never-thrown exception objects
  // the terminate path (noexcept function throws) in a child-free way: the handler exits with 3
  nothrow_term(1);
  printf("not reached\n");
  return 0;
}
