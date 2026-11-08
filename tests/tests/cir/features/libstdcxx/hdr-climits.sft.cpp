//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <climits>: the integer limits macros.
#include <climits>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(CHAR_BIT == 8 && SCHAR_MIN == -128 && SCHAR_MAX == 127 && UCHAR_MAX == 255);
  CHECK(SHRT_MAX == 32767 && USHRT_MAX == 65535 && INT_MAX == 2147483647);
  CHECK(INT_MIN == -2147483647 - 1 && UINT_MAX == 4294967295u);
  CHECK(LONG_MAX == 9223372036854775807L && ULONG_MAX == 18446744073709551615UL);
  CHECK(LLONG_MIN == -9223372036854775807LL - 1 && ULLONG_MAX == 18446744073709551615ULL);
  CHECK(CHAR_MIN == -128 && CHAR_MAX == 127 && MB_LEN_MAX >= 1);
  static_assert(sizeof(long) * CHAR_BIT == 64);
  printf("climits: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
