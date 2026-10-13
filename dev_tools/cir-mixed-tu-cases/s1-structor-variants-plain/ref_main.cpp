#include "common.h"
#include <cstdio>

int main() {
  B *p = make_b();
  std::printf("%d %d\n", sum(p), p->b);
  destroy(p);
  {
    B local;
    std::printf("%d\n", local.a + local.b);
  }
  return 0;
}
