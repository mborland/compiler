//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <string>: string construction, search, replace, conversion and growth past the small buffer.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <string>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::string s = "hello";
  s += ", world";
  CHECK(s.size() == 12 && s == "hello, world");
  CHECK(s.find("world") == 7 && s.find('z') == std::string::npos);
  CHECK(s.substr(7, 3) == "wor");
  s.replace(0, 5, "HELLO");
  CHECK(s.compare("HELLO, world") == 0);
  std::string big(100, 'x');
  big += s;
  CHECK(big.size() == 112 && big.back() == 'd');
  CHECK(std::to_string(-123) == "-123" && std::stoi("  77") == 77);
  CHECK(std::stol("-9000000000") == -9000000000L);
  CHECK(std::stod("2.5") == 2.5);
  std::string t = s + "!" + std::string("?");
  CHECK(t.back() == '?' && t.size() == 14);
  t.insert(0, 3, '-');
  CHECK(t.front() == '-' && t.find_first_not_of('-') == 3);
  t.erase(0, 3);
  CHECK(t.starts_with("HELLO") && t.ends_with("!?"));
#if __cplusplus > 202002L
  CHECK(t.contains("wor"));
#endif
  std::string u8 = "abc";
  std::u16string u16 = u"abc";
  std::u32string u32 = U"abc";
  std::wstring w = L"abc";
  CHECK(u8.size() == 3 && u16.size() == 3 && u32.size() == 3 && w.size() == 3);
  std::string r(s.rbegin(), s.rend());
  CHECK(r.front() == 'd');
  std::string_view sv = s;
  CHECK(sv.size() == s.size());
  CHECK((std::string("a") <=> std::string("b")) < 0);
  printf("string: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
