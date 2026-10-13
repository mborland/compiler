#include "common.h"
#include <cstdio>

void B::f() { A::f(); std::printf("B::f\n"); }
int B::g() { return 9; }

int main() {
  B b;
  std::printf("%d\n", run(&b));
  return 0;
}
