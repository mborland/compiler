//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <complex>: complex arithmetic, polar form and literals.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <complex>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  using namespace std::complex_literals;
  std::complex<double> a(1.0, 2.0), b(3.0, -1.0);
  auto c = a * b;
  CHECK(c.real() == 5.0 && c.imag() == 5.0);
  CHECK((a + b) == std::complex<double>(4.0, 1.0));
  CHECK(std::norm(a) == 5.0);
  CHECK(std::conj(a) == std::complex<double>(1.0, -2.0));
  CHECK(1i * 1i == std::complex<double>(-1.0, 0.0));
  auto q = a / a;
  CHECK(std::abs(q - 1.0) < 1e-12);
  auto p = std::polar(2.0, 0.0);
  CHECK(p.real() == 2.0 && p.imag() == 0.0);
  CHECK(std::abs(std::abs(std::complex<double>(3, 4)) - 5.0) < 1e-12);
  CHECK(std::abs(std::exp(std::complex<double>(0, 0)) - 1.0) < 1e-12);
  std::complex<float> f(1.0f, 1.0f);
  f *= 2.0f;
  CHECK(f.real() == 2.0f && f.imag() == 2.0f);
  CHECK(std::abs(std::sqrt(std::complex<double>(-4, 0)) - std::complex<double>(0, 2)) < 1e-12);
  printf("complex: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
