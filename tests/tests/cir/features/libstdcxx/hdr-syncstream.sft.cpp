//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <syncstream>: osyncstream and syncbuf on a string stream.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <syncstream>
#include <sstream>
#include <iostream>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::ostringstream sink;
  {
    std::osyncstream sync(sink);
    sync << "one ";
    sync << 2;
    CHECK(sink.str().empty());
  }
  CHECK(sink.str() == "one 2");
  std::osyncstream out(std::cout);
  out << "osyncstream line\n";
  out.emit();
  std::ostringstream s2;
  std::osyncstream w(s2);
  w << "x";
  w.emit();
  CHECK(s2.str() == "x" && w.get_wrapped() == s2.rdbuf());
  printf("syncstream: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
