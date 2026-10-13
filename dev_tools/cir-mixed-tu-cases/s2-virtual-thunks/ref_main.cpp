#include "common.h"
#include <cstdio>

int main() {
  D *d = make_d();
  std::printf("%d %d %d\n", call_h(d), d->own(), d->h(3));
  return 0;
}
