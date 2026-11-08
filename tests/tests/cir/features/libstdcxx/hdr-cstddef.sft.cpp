//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cstddef>: size_t, ptrdiff_t, nullptr_t, max_align_t, byte and offsetof.
#include <cstddef>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  static_assert(sizeof(std::size_t) == 8 && sizeof(std::ptrdiff_t) == 8);
  std::nullptr_t np = nullptr;
  int *p = np;
  CHECK(p == nullptr);
  CHECK(alignof(std::max_align_t) >= alignof(long double));
  std::byte b{0x0F};
  b |= std::byte{0xF0};
  CHECK(std::to_integer<int>(b) == 0xFF);
  CHECK(std::to_integer<int>(b << 1 & std::byte{0xFF}) == 0xFE);
  CHECK(std::to_integer<int>(b >> 4) == 0x0F);
  struct S { char c; int i; double d; };
  CHECK(offsetof(S, i) == 4 && offsetof(S, d) == 8);
  std::ptrdiff_t diff = (char *)&np - (char *)&np;
  CHECK(diff == 0);
  std::size_t sz = sizeof(S);
  CHECK(sz == 16);
  printf("cstddef: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
