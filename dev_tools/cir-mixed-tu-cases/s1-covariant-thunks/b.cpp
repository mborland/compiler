#include "common.h"
#include <cstdio>

D *B::get() { return &object; }
D &B::ref() { return object; }
D *B::same() { return &object; }
A *make_b() { return new B; }

int main() {
  A *a = make_b();
  std::printf("%d\n", through_a(a));
  B b;
  D *d = b.get();
  std::printf("%d %d\n", d->d, b.ref().r1);
  delete a;
  return 0;
}
