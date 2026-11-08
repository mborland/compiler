//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <complex.h>: <complex.h> in C++, which is <complex>.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <complex.h>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::complex<double> a(1.0, 1.0);
  CHECK(std::norm(a) == 2.0);
  CHECK((a + a) == std::complex<double>(2.0, 2.0));
  printf("complex.h: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
