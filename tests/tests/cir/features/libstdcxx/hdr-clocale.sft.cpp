//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <clocale>: setlocale and localeconv in the C locale.
#include <clocale>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  const char *loc = std::setlocale(LC_ALL, "C");
  CHECK(loc != nullptr && loc[0] == 'C' && loc[1] == 0);
  std::lconv *lc = std::localeconv();
  CHECK(lc != nullptr && lc->decimal_point[0] == '.' && lc->decimal_point[1] == 0);
  CHECK(lc->thousands_sep[0] == 0);
  CHECK(LC_ALL != LC_NUMERIC && LC_CTYPE != LC_COLLATE);
  CHECK(std::setlocale(LC_NUMERIC, nullptr) != nullptr);
  printf("clocale: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
