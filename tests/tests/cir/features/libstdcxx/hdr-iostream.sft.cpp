//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <iostream>: cout, cerr, clog and cin objects and their interaction with C stdio.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <iostream>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::cout << "cout line " << 42 << ' ' << 1.5 << std::endl;
  CHECK(std::cout.good());
  std::cout << std::boolalpha << true << std::noboolalpha << '\n';
  printf("printf after cout\n");
  std::cout << "cout after printf\n";
  std::cout.flush();
  CHECK(std::cerr.flags() & std::ios::unitbuf);
  CHECK(std::cin.rdbuf() != nullptr && std::clog.tie() == nullptr);
  CHECK(std::cin.tie() == &std::cout);
  CHECK(&std::wcout != nullptr && std::wcerr.good() && std::wclog.good());
  CHECK(std::ios_base::sync_with_stdio(true) == true);
  std::cout << std::hex << 255 << std::dec << ' ' << 255 << '\n';
  printf("iostream: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
