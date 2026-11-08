//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <math.h>: <math.h> in C++, which is <cmath> with the names in the global namespace.
#include <math.h>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(sqrt(9.0) == 3.0 && fabs(-2.0) == 2.0 && floor(1.5) == 1.0);
  CHECK(pow(2.0, 8) == 256.0 && fmod(7.0, 4.0) == 3.0);
  CHECK(isnan(nan("")) && !isinf(1.0) && signbit(-1.0));
  CHECK(std::sqrt(9.0) == ::sqrt(9.0));
  CHECK(abs(-3) == 3 && abs(-2.5) == 2.5);
  printf("math.h: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
