//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cctype>: character classification and conversion.
#include <cctype>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(std::isalpha('a') && !std::isalpha('1') && std::isdigit('7'));
  CHECK(std::isalnum('z') && std::isspace(' ') && std::isspace('\t') && !std::isspace('x'));
  CHECK(std::isupper('A') && std::islower('a') && std::ispunct('!') && !std::ispunct('a'));
  CHECK(std::toupper('q') == 'Q' && std::tolower('Q') == 'q');
  CHECK(std::isxdigit('f') && !std::isxdigit('g') && std::isprint('~') && std::iscntrl('\n'));
  CHECK(std::isgraph('a') && !std::isgraph(' ') && std::isblank('\t'));
  int n = 0;
  for (int c = 0; c < 128; ++c) n += std::isalpha(c) != 0;
  CHECK(n == 52);
  printf("cctype: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
