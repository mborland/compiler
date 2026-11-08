//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <string_view>: string_view slicing, search, comparison and literals.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <string_view>
#include <string>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  using namespace std::literals;
  std::string_view sv = "hello world";
  CHECK(sv.size() == 11 && sv.front() == 'h' && sv.back() == 'd');
  CHECK(sv.substr(6) == "world" && sv.find("o w") == 4);
  sv.remove_prefix(6);
  CHECK(sv == "world");
  sv.remove_suffix(2);
  CHECK(sv == "wor" && sv.starts_with('w') && sv.ends_with("or"));
  CHECK("abc"sv < "abd"sv && "abc"sv.compare("abc") == 0);
  CHECK(sv.find_first_of("ro") == 1 && sv.rfind('r') == 2);
  constexpr std::string_view c = "constexpr";
  static_assert(c.size() == 9 && c[0] == 'c');
  CHECK(std::hash<std::string_view>{}("x") == std::hash<std::string_view>{}("x"));
  std::string_view e;
  CHECK(e.empty() && e.data() == nullptr);
  CHECK(L"wide"sv.size() == 4 && u8"u8"sv.size() == 2);
  printf("string_view: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
