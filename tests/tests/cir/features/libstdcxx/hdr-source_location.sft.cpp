//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <source_location>: source_location::current and its accessors.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <source_location>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
std::source_location where(std::source_location l = std::source_location::current()) { return l; }
int main() {
  auto here = std::source_location::current();
  CHECK(here.line() == __LINE__ - 1);
  CHECK(here.column() > 0);
  CHECK(here.file_name() != nullptr && here.file_name()[0] != 0);
  CHECK(here.function_name() != nullptr);
  auto l = where();
  CHECK(l.line() == __LINE__ - 1);
  CHECK(sizeof(std::source_location) > 0);
  printf("source_location: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
