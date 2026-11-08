//type:rp
//options:--c++20 --diag_suppress=cir_warning:--c++20 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <codecvt>: codecvt_utf8 conversions between UTF-8 and UTF-32 (deprecated).
#include <codecvt>
#include <locale>
#include <cwchar>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::codecvt_utf8<char32_t> cv;
  std::mbstate_t st{};
  const char32_t in[] = {U'a', char32_t(0xE9), char32_t(0x20AC), char32_t(0x1F600)};
  char out[16] = {};
  const char32_t *inext;
  char *onext;
  auto r = cv.out(st, in, in + 4, inext, out, out + sizeof out, onext);
  CHECK(r == std::codecvt_base::ok && onext - out == 1 + 2 + 3 + 4);
  CHECK((unsigned char)out[1] == 0xC3 && (unsigned char)out[2] == 0xA9);
  char32_t back[8] = {};
  std::mbstate_t st2{};
  const char *fnext;
  char32_t *bnext;
  r = cv.in(st2, out, onext, fnext, back, back + 8, bnext);
  CHECK(r == std::codecvt_base::ok && bnext - back == 4);
  CHECK(back[2] == 0x20AC && back[3] == 0x1F600);
  CHECK(cv.max_length() == 4);
  std::codecvt_utf8_utf16<char16_t> c16;
  CHECK(c16.max_length() == 4);
  printf("codecvt: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
