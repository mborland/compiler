//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cuchar>: mbrtoc16, mbrtoc32, c16rtomb and c32rtomb on ASCII and UTF-8.
#include <cuchar>
#include <climits>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::mbstate_t st{};
  char32_t c32 = 0;
  CHECK(std::mbrtoc32(&c32, "A", 1, &st) == 1 && c32 == U'A');
  char16_t c16 = 0;
  CHECK(std::mbrtoc16(&c16, "z", 1, &st) == 1 && c16 == u'z');
  char buf[MB_LEN_MAX];
  std::mbstate_t s2{};
  CHECK(std::c32rtomb(buf, U'Q', &s2) == 1 && buf[0] == 'Q');
  std::mbstate_t s3{};
  CHECK(std::c16rtomb(buf, u'k', &s3) == 1 && buf[0] == 'k');
  std::mbstate_t s4{};
  CHECK(std::mbrtoc32(&c32, "", 1, &s4) == 0 && c32 == 0);
  printf("cuchar: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
