//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <ostream>: formatted and unformatted output to a string stream.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <ostream>
#include <string>
#include <sstream>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
struct User { int v; };
static std::ostream &operator<<(std::ostream &os, User u) { return os << "user" << u.v; }
int main() {
  std::ostringstream o;
  o << 42 << ' ' << -7 << ' ' << 3.5 << ' ' << true << ' ' << 'c' << ' ' << "str";
  CHECK(o.str() == "42 -7 3.5 1 c str");
  o.str("");
  o << std::boolalpha << true << ' ' << std::hex << 255 << ' ' << std::oct << 8 << std::dec;
  CHECK(o.str() == "true ff 10");
  o.str("");
  o.width(6);
  o.fill('*');
  o << std::left << 12 << '|' << std::right << 34;
  CHECK(o.str() == "12****|34");
  o.str("");
  o << std::scientific << 1234.5 << ' ' << std::fixed << 2.0;
  CHECK(o.str() == "1.234500e+03 2.000000");
  o.str("");
  o.put('a').put('b');
  o.write("cdef", 3);
  CHECK(o.str() == "abcde" && o.tellp() == 5);
  o << User{3} << std::endl;
  CHECK(o.str() == "abcdeuser3\n");
  o.str("");
  o << (void *)0 << ' ' << 12345678901LL << ' ' << 0.1f << ' ' << 1e100;
  CHECK(o.str().size() > 10);
  o.str("");
  o << std::showpos << 5 << std::noshowpos << ' ' << std::uppercase << std::hex << 255;
  CHECK(o.str() == "+5 FF");
  printf("ostream: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
