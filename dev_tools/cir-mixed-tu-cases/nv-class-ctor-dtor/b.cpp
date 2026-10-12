#include "common.h"
#include <cstdio>

int main() {
  int result;
  {
    Counter a;
    Counter b(7);
    Counter c(b);
    Counter d = make_counter(40);
    result = a.get() + b.get() + c.get() % 1000 + d.get() % 1000;
    Counter *heap = new Counter(5);
    heap->add(3);
    result += heap->get();
    delete heap;
    std::printf("live inside: %d\n", Counter::live);
  }
  result += sum_array(4);
  std::printf("result %d live %d made>0 %d\n", result, Counter::live,
              Counter::made > 0);
  return Counter::live;
}
