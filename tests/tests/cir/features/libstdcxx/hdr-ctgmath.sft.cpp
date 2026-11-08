//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <ctgmath>: the deprecated <ctgmath>, which includes <cmath> and <complex>.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <ctgmath>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(std::sqrt(4.0) == 2.0 && std::pow(2.0, 3) == 8.0);
  std::complex<double> c(3.0, 4.0);
  CHECK(std::abs(c) == 5.0);
  printf("ctgmath: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
