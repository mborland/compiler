//type:fn
//options:--c++23 -lstdc++exp --diag_suppress=cir_warning:--c++23 -O2 -lstdc++exp --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <stacktrace>: stacktrace::current and its entries (C++23; libstdc++exp).
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <stacktrace>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  auto st = std::stacktrace::current();
  CHECK(st.size() == (std::size_t)std::distance(st.begin(), st.end()));
  std::stacktrace empty;
  CHECK(empty.empty() && empty.size() == 0);
  CHECK(st == st);
  CHECK(std::stacktrace_entry() == std::stacktrace_entry());
  printf("stacktrace: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
