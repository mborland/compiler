//type:rp
//options:--c++26 --diag_suppress=cir_warning:--c++26 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <stdbit.h>: <stdbit.h> bit utilities (C++26).
#include <stdbit.h>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(stdc_count_ones(0xF0F0u) == 8 && stdc_count_zeros(0xFFFFFFFEu) == 1);
  CHECK(stdc_leading_zeros(1u) == 31 && stdc_trailing_zeros(8u) == 3);
  CHECK(stdc_first_leading_one(0x80000000u) == 1 && stdc_first_trailing_one(8u) == 4);
  CHECK(stdc_bit_width(255u) == 8 && stdc_has_single_bit(64u));
  CHECK(stdc_bit_ceil(65u) == 128u && stdc_bit_floor(65u) == 64u);
  CHECK(stdc_leading_ones(0xFF000000u) == 8 && stdc_trailing_ones(7u) == 3);
  printf("stdbit.h: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
