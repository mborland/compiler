//type:fc
//options:--c++23 --set_flag=coroutines --diag_suppress=cir_warning:--c++23 -O2 --set_flag=coroutines --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <generator>: std::generator, a coroutine (C++23).
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <generator>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
std::generator<int> count_to(int n) {
  for (int i = 1; i <= n; ++i) co_yield i;
}
int main() {
  int sum = 0;
  for (int v : count_to(5)) sum += v;
  CHECK(sum == 15);
  auto g = count_to(3);
  auto it = g.begin();
  CHECK(*it == 1);
  ++it;
  CHECK(*it == 2);
  printf("generator: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
