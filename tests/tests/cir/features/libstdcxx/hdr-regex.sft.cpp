//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <regex>: regex_match, regex_search, match results, regex_replace and iterators.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <regex>
#include <string>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::regex re("([a-z]+)@([a-z]+)\\.com");
  std::smatch m;
  std::string s = "mail bob@example.com now";
  CHECK(std::regex_search(s, m, re));
  CHECK(m.size() == 3 && m[1] == "bob" && m[2] == "example" && m.position(0) == 5);
  CHECK(std::regex_match("abc123", std::regex("[a-z]+[0-9]+")));
  CHECK(!std::regex_match("abc", std::regex("[0-9]+")));
  CHECK(std::regex_replace("a1b22c", std::regex("[0-9]+"), "#") == "a#b#c");
  int n = 0;
  std::regex word("[a-z]+");
  for (std::sregex_iterator it(s.begin(), s.end(), word), e; it != e; ++it) ++n;
  CHECK(n == 5);
  std::regex ic("HELLO", std::regex::icase);
  CHECK(std::regex_search("say hello", ic));
  bool caught = false;
  try { std::regex bad("("); } catch (const std::regex_error &) { caught = true; }
  CHECK(caught);
  std::cmatch cm;
  CHECK(std::regex_match("x=42", cm, std::regex("(\\w)=(\\d+)")) && cm[2].str() == "42");
  printf("regex: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
