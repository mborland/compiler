//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cmath>: the <cmath> functions, classification and special functions.
#include <cmath>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
static bool near(double a, double b) { double d = a - b; return (d < 0 ? -d : d) < 1e-9; }
static double zero() { volatile double z = 0.0; return z; }
int main() {
  CHECK(near(std::sqrt(2.0), 1.4142135623731));
  CHECK(near(std::pow(2.0, 10), 1024.0) && near(std::sin(0.0), 0.0));
  CHECK(std::floor(-1.5) == -2.0 && std::ceil(-1.5) == -1.0 && std::trunc(-1.5) == -1.0);
  CHECK(std::fmod(7.0, 3.0) == 1.0 && std::fabs(-3.0) == 3.0);
  CHECK(near(std::hypot(3.0, 4.0), 5.0) && near(std::hypot(1.0, 2.0, 2.0), 3.0));
  CHECK(std::isnan(std::nan("")) && std::isinf(1.0 / zero()) && std::isfinite(1.0));
  CHECK(std::signbit(-0.0) && !std::signbit(0.0));
  CHECK(std::lround(2.5) == 3 && std::llround(-2.5) == -3);
  CHECK(near(std::fma(2.0, 3.0, 1.0), 7.0) && near(std::cbrt(27.0), 3.0));
  CHECK(std::copysign(3.0, -1.0) == -3.0);
  CHECK(near(std::exp(1.0), 2.71828182845905) && near(std::log(std::exp(2.0)), 2.0));
  CHECK(near(std::atan2(1.0, 1.0) * 4, 3.14159265358979));
  CHECK(near(std::lerp(10.0, 20.0, 0.25), 12.5));
  CHECK(near(std::legendre(2, 0.5), -0.125) && near(std::hermite(2, 1.0), 2.0));
  CHECK(near(std::riemann_zeta(2.0), 1.64493406684823));
  CHECK(std::fpclassify(1.0f) == FP_NORMAL && std::ilogb(8.0) == 3);
  CHECK(std::nextafter(1.0, 2.0) > 1.0 && std::remainder(7.0, 4.0) == -1.0);
  CHECK(std::abs(-5) == 5 && std::abs(-5.5f) == 5.5f);
  CHECK(near(std::expm1(1e-10), 1e-10) && near(std::log1p(1e-10), 1e-10));
  CHECK(std::isunordered(std::nan(""), 1.0));
  printf("cmath: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
