//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <tgmath.h>: <tgmath.h> in C++, which is <ctgmath>.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <tgmath.h>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(std::sqrt(16.0) == 4.0);
  std::complex<double> z(0.0, 1.0);
  CHECK(std::abs(z) == 1.0);
  printf("tgmath.h: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
