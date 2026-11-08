//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <istream>: formatted and unformatted input from a string stream.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <istream>
#include <sstream>
#include <string>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::istringstream in("12 3.5 word\nsecond line\n  last");
  int i; double d; std::string w;
  in >> i >> d >> w;
  CHECK(i == 12 && d == 3.5 && w == "word");
  CHECK(in.get() == '\n');
  std::string line;
  std::getline(in, line);
  CHECK(line == "second line");
  CHECK(in.peek() == ' ');
  in >> std::ws;
  CHECK(in.peek() == 'l');
  char c3[4] = {};
  in.read(c3, 3);
  CHECK(in.gcount() == 3 && c3[2] == 's');
  in.unget();
  CHECK(in.get() == 's');
  in.ignore(10);
  in >> i;
  CHECK(in.fail());
  std::istringstream bad("abc");
  bad >> i;
  CHECK(bad.fail() && !bad);
  std::istringstream hx("ff 077");
  hx >> std::hex >> i;
  CHECK(i == 255);
  std::istringstream sk("a b");
  char x, y;
  sk >> std::noskipws >> x >> y;
  CHECK(x == 'a' && y == ' ');
  printf("istream: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
