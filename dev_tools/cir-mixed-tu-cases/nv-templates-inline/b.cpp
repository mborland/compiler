#include "common.h"
#include <cstdio>

int main() {
  Box<int> b(5);
  Box<double> d(1.5);
  Box<int>::instances += 1;
  Box<double>::instances += 7;
  long x = a_inline(10);
  long y = inline_sum(10, 1);
  long z = a_inline(10);
  int w = a_work();
  std::printf("%d %g %ld %ld %ld %d %d %d\n", twice(b.get()),
              d.cast_add(2.0), x, y, z, w, Box<int>::instances,
              Box<double>::instances);
  return 0;
}
