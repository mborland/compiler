//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <numbers>: the mathematical constants.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <numbers>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
static bool close(double a, double b) { double d = a - b; return (d < 0 ? -d : d) < 1e-9; }
int main() {
  CHECK(close(std::numbers::pi, 3.14159265358979));
  CHECK(close(std::numbers::e, 2.71828182845905));
  CHECK(close(std::numbers::sqrt2 * std::numbers::sqrt2, 2.0));
  CHECK(close(std::numbers::inv_pi * std::numbers::pi, 1.0));
  CHECK(close(std::numbers::phi * std::numbers::phi, std::numbers::phi + 1));
  CHECK(close(std::numbers::ln2, 0.693147180559945));
  CHECK(close(std::numbers::pi_v<float>, 3.14159265f));
  CHECK(close(std::numbers::sqrt3 * std::numbers::inv_sqrt3, 1.0));
  CHECK(close(std::numbers::log2e * std::numbers::ln2, 1.0));
  CHECK(close(std::numbers::egamma, 0.577215664901533));
  static_assert(std::is_same_v<decltype(std::numbers::pi), const double>);
  printf("numbers: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
