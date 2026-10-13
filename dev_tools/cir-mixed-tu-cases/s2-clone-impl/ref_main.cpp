#include "common.h"
#include <cstdio>

int main() {
  clone_base const *a = make_a(5);
  clone_base const *b = make_b(7);
  std::printf("%d %d %d %d\n", a->get(), b->get(), twice_clone(a),
              twice_clone(b));
  delete a;
  delete b;
  return 0;
}
