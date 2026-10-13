#include "common.h"
#include <cstdio>

B::~B() { std::printf("~B\n"); }
A *make_b() { return new B; }

int main() {
  destroy(make_b());
  {
    B local;
  }
  A *p = make_b();
  delete p;
  return 0;
}
