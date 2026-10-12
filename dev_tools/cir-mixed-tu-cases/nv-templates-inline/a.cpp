#include "common.h"

template struct Box<int>;

int a_work() {
  Box<int> b(20);
  Box<int>::instances += 2;
  return twice(b.get()) + b.cast_add(2L) + twice(3) + Box<int>::instances;
}

long a_inline(long x) { return inline_sum(x, 1); }
