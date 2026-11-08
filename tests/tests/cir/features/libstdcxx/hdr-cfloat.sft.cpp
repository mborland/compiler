//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cfloat>: the floating-point characteristic macros.
#include <cfloat>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && DBL_MANT_DIG == 53 && LDBL_MANT_DIG == 64);
  CHECK(FLT_DIG == 6 && DBL_DIG == 15 && LDBL_DIG == 18);
  CHECK(DBL_EPSILON == 2.220446049250313e-16 && FLT_EPSILON == 1.1920929e-07f);
  CHECK(DBL_MAX > 1e308 && DBL_MIN > 0 && DBL_MIN < 1e-307);
  CHECK(FLT_MAX > 3e38f && FLT_MAX_EXP == 128 && DBL_MAX_EXP == 1024);
  CHECK(FLT_EVAL_METHOD == 0 && DECIMAL_DIG == 21);
  CHECK(DBL_MAX_10_EXP == 308 && FLT_MIN_EXP == -125);
  CHECK(LDBL_MAX > DBL_MAX);
  printf("cfloat: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
