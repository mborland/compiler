#include "common.h"
#include <cstdio>

int main() {
  std::printf("%d %d\n", call_g(make_c()), call_f(make_c_as_a1()));
  return 0;
}
