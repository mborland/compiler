#include "common.h"
#include <cstdio>

int main() {
  D *d = make_d();
  A *a = d;
  B *b = d;
  C *c = d;
  std::printf("%d %d %d\n", a->f(), b->f(), c->g());
  std::printf("%d %d\n", use_b(b), use_c(c));
  destroy(a);
  return 0;
}
