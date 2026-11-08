//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <limits>: numeric_limits of the integer and floating types.
#include <limits>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(std::numeric_limits<int>::max() == 2147483647);
  CHECK(std::numeric_limits<int>::min() == -2147483647 - 1);
  CHECK(std::numeric_limits<unsigned char>::max() == 255);
  CHECK(std::numeric_limits<long long>::digits == 63);
  CHECK(std::numeric_limits<unsigned>::digits == 32 && !std::numeric_limits<unsigned>::is_signed);
  CHECK(std::numeric_limits<double>::digits == 53);
  CHECK(std::numeric_limits<float>::epsilon() == 1.1920929e-07f);
  CHECK(std::numeric_limits<double>::epsilon() == 2.220446049250313e-16);
  CHECK(std::numeric_limits<double>::has_infinity);
  double inf = std::numeric_limits<double>::infinity();
  CHECK(inf > std::numeric_limits<double>::max());
  double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(nan != nan);
  CHECK(std::numeric_limits<double>::lowest() == -std::numeric_limits<double>::max());
  CHECK(std::numeric_limits<long double>::digits == 64);
  CHECK(std::numeric_limits<char>::is_signed && std::numeric_limits<bool>::max());
  CHECK(std::numeric_limits<__int128>::digits == 127);
  CHECK(std::numeric_limits<double>::denorm_min() > 0.0);
  printf("limits: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
