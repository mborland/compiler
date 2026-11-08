//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <stdfloat>: a std::float128_t constant, which the CIR back end cannot express (a half or quad constant).
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <stdfloat>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::float128_t c = 1.0f128;
  CHECK(c / 4 == 0.25f128);
  printf("stdfloat: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
