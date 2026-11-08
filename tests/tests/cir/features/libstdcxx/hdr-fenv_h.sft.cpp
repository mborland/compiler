//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <fenv.h>: <fenv.h> in C++, which is <cfenv> with the names in the global namespace.
#include <fenv.h>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(fegetround() == FE_TONEAREST);
  CHECK(fesetround(FE_DOWNWARD) == 0 && fegetround() == FE_DOWNWARD);
  fesetround(FE_TONEAREST);
  CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
  CHECK(std::fegetround() == ::fegetround());
  printf("fenv.h: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
