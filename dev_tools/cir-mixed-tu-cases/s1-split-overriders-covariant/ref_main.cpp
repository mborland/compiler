#include "common.h"
#include <cstdio>

int main() {
  A2 *p = make_c();
  std::printf("%d %d %d\n", through_a2(p), subobject_ok(p) ? 1 : 0,
              call_f(make_c_as_a1()));
  return 0;
}
