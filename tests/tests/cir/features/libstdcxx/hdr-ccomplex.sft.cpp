//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <ccomplex>: the deprecated <ccomplex>, which includes <complex>.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <ccomplex>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::complex<double> a(1.0, 2.0);
  CHECK((a * a).real() == -3.0 && (a * a).imag() == 4.0);
  CHECK(std::norm(a) == 5.0);
  printf("ccomplex: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
