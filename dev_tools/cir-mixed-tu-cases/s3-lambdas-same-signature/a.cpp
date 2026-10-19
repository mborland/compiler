#include "common.h"
#include <cstdio>

int main() {
  int a = x();
  int b = f();
  int c = x();
  std::printf("%d %d %d\n", a, b, c);
  return 0;
}
