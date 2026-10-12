#include "p.h"
#include <new>
void *T::operator new(unsigned long n) { printf("T::new\n"); return ::operator new(n); }
void T::operator delete(void *p) { printf("T::delete\n"); ::operator delete(p); }
void *T::operator new[](unsigned long n) { printf("T::new[]\n"); return ::operator new(n); }
void T::operator delete[](void *p) { printf("T::delete[]\n"); ::operator delete(p); }
int mk(int k) { if (k < 0) { printf("mk throws\n"); throw k; } return k; }
int new_two_temps(int fail) {
  try { T *p = new T{A(1), B(2), fail}; int r = p->v; delete p; return r; } catch (int i) { return i; }
}
int throw_with_temp(int fail) {
  try { throw T(A(3), fail); } catch (const T &t) { return t.v; } catch (int i) { return i + 100; }
}
int new_array_init(int fail) {
  try { U *p = new U[3]{U(A(1), 0), U(A(2), fail), U()}; int r = p[0].v; delete[] p; return r; } catch (int i) { return i; }
}
int cond_new(int c, int fail) {
  try { T *p = c ? new T(A(4), fail) : nullptr; int r = p ? p->v : -1; delete p; return r; } catch (int i) { return i; }
}
int cond_new2(int c, int fail) {
  try { T *p = new T{A(5), c ? B(6) : B(7), fail}; int r = p->v; delete p; return r; } catch (int i) { return i; }
}
int new_arg_throws(int fail) {
  try { T *p = new T{A(8), B(mk(-fail)), 0}; delete p; return 0; } catch (int i) { return i; }
}
int run_all() {
  int s = 0;
  for (int f = 0; f < 2; ++f) { printf("-- new_two_temps %d\n", f); s += new_two_temps(f * 7); }
  for (int f = 0; f < 2; ++f) { printf("-- throw_with_temp %d\n", f); s += throw_with_temp(f * 5); }
  for (int f = 0; f < 2; ++f) { printf("-- new_array_init %d\n", f); s += new_array_init(f * 9); }
  for (int c = 0; c < 2; ++c) for (int f = 0; f < 2; ++f) { printf("-- cond_new %d %d\n", c, f); s += cond_new(c, f * 3); }
  for (int c = 0; c < 2; ++c) for (int f = 0; f < 2; ++f) { printf("-- cond_new2 %d %d\n", c, f); s += cond_new2(c, f * 3); }
  for (int f = 0; f < 2; ++f) { printf("-- new_arg_throws %d\n", f); s += new_arg_throws(f); }
  return s;
}
