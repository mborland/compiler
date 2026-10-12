#include "p.h"
#include <new>
void *T::operator new(unsigned long n) { printf("T::new\n"); return ::operator new(n); }
void T::operator delete(void *p) { printf("T::delete\n"); ::operator delete(p); }
void *T::operator new[](unsigned long n) { return ::operator new(n); }
void T::operator delete[](void *p) { ::operator delete(p); }
int new_two_temps(int) { return 0; }
int throw_with_temp(int) { return 0; }
int new_array_init(int) { return 0; }
int cond_new2(int, int) { return 0; }
int new_arg_throws(int) { return 0; }
int cond_new(int c, int fail) {
  try { T *p = c ? new T(A(4), fail) : nullptr; int r = p ? p->v : -1; delete p; return r; } catch (int i) { return i; }
}
int run_all() {
  int s = 0;
  for (int c = 0; c < 2; ++c) for (int f = 0; f < 2; ++f) { printf("-- cond_new %d %d\n", c, f); s += cond_new(c, f * 3); }
  return s;
}
