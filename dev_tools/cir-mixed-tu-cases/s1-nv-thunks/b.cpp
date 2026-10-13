#include "common.h"
#include <cstdio>

int C::f() { return 40 + c; }
int C::g(int x) { return x + a1 + a2 + c; }
int C::h(int x, double y) { return x * 2 + (int)y + a2; }
C::~C() { std::printf("~C\n"); }
A2 *make_c_as_a2() { return new C; }

int main() {
  A2 *p = make_c_as_a2();
  std::printf("%d\n", call_a2(p));
  C c;
  A1 *a1 = &c;
  A2 *a2 = &c;
  std::printf("%d %d %d\n", a1->f(), a2->g(10), a2->h(1, 2.5));
  delete p;
  return 0;
}
