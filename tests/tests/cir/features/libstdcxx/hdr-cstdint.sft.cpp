//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cstdint>: fixed-width integer types, limits macros and constant macros.
#include <cstdint>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  static_assert(sizeof(std::int8_t) == 1 && sizeof(std::int16_t) == 2);
  static_assert(sizeof(std::int32_t) == 4 && sizeof(std::int64_t) == 8);
  static_assert(sizeof(std::intptr_t) == sizeof(void *) && sizeof(std::intmax_t) == 8);
  CHECK(INT8_MAX == 127 && INT16_MIN == -32768 && UINT32_MAX == 4294967295u);
  CHECK(INT64_MAX == 9223372036854775807LL && UINT64_MAX == 18446744073709551615ULL);
  CHECK(UINT32_C(7) + 1 == 8 && INT64_C(-5) < 0 && INTMAX_C(3) == 3);
  std::uint_least16_t l = 65535;
  std::uint_fast8_t f = 200;
  CHECK(l == 65535 && f == 200);
  std::uint8_t u = 255;
  u++;
  CHECK(u == 0);
  CHECK(SIZE_MAX == 18446744073709551615UL && PTRDIFF_MAX == 9223372036854775807L);
  std::int32_t s = INT32_MIN;
  CHECK(s < 0 && (std::uint32_t)s == 2147483648u);
  printf("cstdint: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
