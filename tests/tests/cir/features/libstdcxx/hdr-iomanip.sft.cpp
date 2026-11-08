//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <iomanip>: setw, setfill, setprecision, setbase, quoted, put_money and resetiosflags.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <iomanip>
#include <sstream>
#include <string>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::ostringstream o;
  o << std::setw(6) << std::setfill('0') << 42;
  CHECK(o.str() == "000042");
  o.str("");
  o << std::setprecision(3) << 3.14159 << ' ' << std::fixed << std::setprecision(2) << 2.0;
  CHECK(o.str() == "3.14 2.00");
  o.str("");
  o << std::setbase(16) << 255 << ' ' << std::setbase(8) << 8 << std::setbase(10);
  CHECK(o.str() == "ff 10");
  o.str("");
  o << std::quoted("a b\"c");
  CHECK(o.str() == "\"a b\\\"c\"");
  std::istringstream i("\"x y\" z");
  std::string s;
  i >> std::quoted(s);
  CHECK(s == "x y");
  o.str("");
  o << std::setiosflags(std::ios::showpos) << 5 << std::resetiosflags(std::ios::showpos) << 6;
  CHECK(o.str() == "+56");
  o.str("");
  o << std::setfill(' ') << std::left << std::setw(5) << "ab" << '|' << std::right << std::setw(5) << "cd";
  CHECK(o.str() == "ab   |   cd");
  printf("iomanip: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
