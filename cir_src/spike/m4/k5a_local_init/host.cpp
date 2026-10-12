#include "p.h"
extern "C" {
int __real___cxa_guard_acquire(long long *); void __real___cxa_guard_release(long long *); void __real___cxa_guard_abort(long long *);
static int acq = 0, rel = 0, abrt = 0;
int __wrap___cxa_guard_acquire(long long *g) { ++acq; return __real___cxa_guard_acquire(g); }
void __wrap___cxa_guard_release(long long *g) { ++rel; __real___cxa_guard_release(g); }
void __wrap___cxa_guard_abort(long long *g) { ++abrt; __real___cxa_guard_abort(g); }
}
int main() {
  int s = 0;
  try { s += once(3); } catch (int i) { printf("once threw %d\n", i); s += 1000; }
  s += once(3);
  s += once(4);
  try { s += twice(2); } catch (int i) { printf("twice threw %d\n", i); }
  s += twice(2);
  printf("ctor_static %d %d\n", ctor_static(1), ctor_static(2));
  printf("inline %d\n", inline_static(1));
  printf("sum %d guards acquire %d release %d abort %d\n", s, acq, rel, abrt);
  return 0;
}
