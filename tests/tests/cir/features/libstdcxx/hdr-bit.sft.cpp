//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <bit>: popcount, countl_zero, countr_zero, rotl, rotr, byteswap, bit_ceil and endian.
#include <bit>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(std::popcount(0xF0F0u) == 8 && std::popcount(0ull) == 0);
  CHECK(std::countl_zero(1u) == 31 && std::countr_zero(8u) == 3);
  CHECK(std::countl_one(0xFF000000u) == 8 && std::countr_one(7u) == 3);
  CHECK(std::rotl(0x80000001u, 1) == 3u && std::rotr(3u, 1) == 0x80000001u);
#if __cplusplus > 202002L
  CHECK(std::byteswap(0x01020304u) == 0x04030201u);
#endif
  CHECK(std::has_single_bit(64u) && !std::has_single_bit(65u));
  CHECK(std::bit_ceil(65u) == 128u && std::bit_floor(65u) == 64u);
  CHECK(std::bit_width(255u) == 8);
  CHECK(std::endian::native == std::endian::little);
  CHECK(std::popcount((unsigned char)0xFF) == 8);
  volatile unsigned long long v = 1ull << 40;
  CHECK(std::countr_zero((unsigned long long)v) == 40);
  printf("bit: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
