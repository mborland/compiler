#include "common.h"
#include <cstdio>
#include <typeinfo>

int main() {
  A *a = make_a();
  A *b = make_b();
  C *c = make_d_as_c();
  std::printf("%d %d %d\n", typeid(*a) == typeid(A), typeid(*b) == typeid(B),
              typeid(*c) == typeid(D));
  std::printf("%d %d\n", typeid(*b) == typeid(A), typeid(*a) == typeid(B));
  std::printf("%d %d %d\n", dynamic_cast<B *>(a) != 0,
              dynamic_cast<B *>(b) != 0, dynamic_cast<D *>(c) != 0);
  std::printf("%s %s\n", typeid(*a).name(), typeid(*c).name());
  delete a;
  delete b;
  delete c;
  return 0;
}
