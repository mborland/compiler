#include "common.h"
#include <cstdio>

int main() {
  A *p = make_b();
  std::printf("%d\n", use(p));
  destroy(p);
  {
    B local;
    std::printf("%d\n", local.f());
  }
  return 0;
}
