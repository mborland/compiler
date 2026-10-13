#include "common.h"
#include <cstdio>

int B::f() { return 200 + b; }
int B::h() { return 7; }
A *make_b() { return new B; }

int main() {
  A *a = make_a();
  A *b = make_b();
  B local;
  std::printf("%d %d %d\n", sum_a(a), sum_a(b), local.h() + local.f());
  delete a;
  delete b;
  return 0;
}
