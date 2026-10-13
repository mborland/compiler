#include "common.h"
#include <cstdio>

void B::f() { std::printf("B::f\n"); }

int main() {
  std::fflush(stdout);
  B b;
  std::printf("not reached\n");
  return 0;
}
