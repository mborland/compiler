//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cinttypes>: format macros, imaxabs, imaxdiv and strtoimax.
#include <cinttypes>
#include <cstdio>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%" PRId64 " %" PRIu32 " %" PRIx8, (std::int64_t)-5, (std::uint32_t)7, (std::uint8_t)255);
  CHECK(buf[0] == '-' && buf[1] == '5' && buf[3] == '7' && buf[5] == 'f');
  std::int64_t a = 0;
  std::uint32_t b = 0;
  CHECK(std::sscanf("123 456", "%" SCNd64 " %" SCNu32, &a, &b) == 2 && a == 123 && b == 456);
  CHECK(std::imaxabs(-9) == 9);
  std::imaxdiv_t d = std::imaxdiv(17, 5);
  CHECK(d.quot == 3 && d.rem == 2);
  CHECK(std::strtoimax("-77", nullptr, 10) == -77 && std::strtoumax("77", nullptr, 8) == 63);
  printf("cinttypes: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
