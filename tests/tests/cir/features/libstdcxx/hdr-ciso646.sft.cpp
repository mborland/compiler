//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <ciso646>: the deprecated <ciso646> (alternative tokens are keywords in C++).
#include <ciso646>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  int a = 6, b = 3;
  CHECK((a bitand b) == 2 and (a bitor b) == 7 and (a xor b) == 5);
  CHECK(not (a == b) and (compl a) == ~6);
  bool x = true;
  x and_eq false;
  CHECK(!x);
  printf("ciso646: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
