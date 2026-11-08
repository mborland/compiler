//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cstdbool>: the deprecated <cstdbool>.
#include <cstdbool>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
#ifdef __bool_true_false_are_defined
  CHECK(__bool_true_false_are_defined == 1);
#endif
  bool b = true;
  CHECK(b && !false);
  CHECK(sizeof(bool) == 1);
  printf("cstdbool: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
