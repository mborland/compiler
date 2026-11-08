//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cfenv>: floating-point environment: rounding mode round trips and exception flags.
#include <cfenv>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(std::fegetround() == FE_TONEAREST);
  CHECK(std::fesetround(FE_UPWARD) == 0 && std::fegetround() == FE_UPWARD);
  CHECK(std::fesetround(FE_TOWARDZERO) == 0 && std::fegetround() == FE_TOWARDZERO);
  CHECK(std::fesetround(FE_TONEAREST) == 0 && std::fegetround() == FE_TONEAREST);
  CHECK(std::feclearexcept(FE_ALL_EXCEPT) == 0 && std::fetestexcept(FE_ALL_EXCEPT) == 0);
  CHECK(std::feraiseexcept(FE_DIVBYZERO) == 0 && (std::fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO));
  std::feclearexcept(FE_DIVBYZERO);
  CHECK(!std::fetestexcept(FE_DIVBYZERO));
  std::fenv_t env;
  CHECK(std::fegetenv(&env) == 0 && std::fesetenv(&env) == 0);
  std::fexcept_t fl;
  CHECK(std::fegetexceptflag(&fl, FE_ALL_EXCEPT) == 0);
  printf("cfenv: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
