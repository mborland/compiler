//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <stdlib.h>: <stdlib.h> in C++, which is <cstdlib> with the names in the global namespace.
#include <stdlib.h>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(abs(-3) == 3 && atoi("12") == 12 && strtol("77", nullptr, 10) == 77);
  div_t d = div(9, 4);
  CHECK(d.quot == 2 && d.rem == 1);
  void *p = malloc(8);
  CHECK(p != nullptr);
  free(p);
  CHECK(std::abs(-3) == ::abs(-3));
  printf("stdlib.h: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
