//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <print>: print and println to stdout (C++23).
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <print>
#include <cstdio>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::print("print {} {}\n", 1, "two");
  std::println("println {:>4}", 42);
  std::println();
  std::print(stdout, "to {}\n", "stdout");
  std::println(stdout, "pi {:.2f}", 3.14159);
  std::print("{}{}", 'a', 'b');
  std::println("");
  int seven = 7;
  std::vprint_unicode("unicode {}\n", std::make_format_args(seven));
  printf("print: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
