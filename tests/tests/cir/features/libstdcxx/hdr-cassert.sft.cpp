//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cassert>: assert with NDEBUG undefined, assert that holds, static_assert.
#include <cassert>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  assert(1 + 1 == 2);
  int x = 3;
  assert(x == 3 && "message");
  static_assert(true, "static");
  CHECK(x == 3);
  bool evaluated = false;
  assert((evaluated = true));
  CHECK(evaluated);
  printf("cassert: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
