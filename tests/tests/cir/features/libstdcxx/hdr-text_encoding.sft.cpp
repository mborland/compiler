//type:fn
//options:--c++26 --diag_suppress=cir_warning:--c++26 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <text_encoding>: text_encoding ids, names and the literal encoding (C++26).
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <text_encoding>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::text_encoding utf8 = std::text_encoding(std::text_encoding::UTF8);
  CHECK(utf8.mib() == std::text_encoding::UTF8 && utf8 == std::text_encoding::UTF8);
  CHECK(__builtin_strcmp(utf8.name(), "UTF-8") == 0);
  std::text_encoding unk;
  CHECK(unk.mib() == std::text_encoding::unknown);
  std::text_encoding ascii(std::text_encoding::ASCII);
  CHECK(ascii != utf8 && ascii.mib() == std::text_encoding::ASCII);
  std::text_encoding lit = std::text_encoding::literal();
  CHECK(lit.mib() == std::text_encoding::UTF8);
  std::text_encoding named("UTF-8");
  CHECK(named == utf8);
  printf("text_encoding: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
