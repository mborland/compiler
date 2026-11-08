//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <latch>: latch count down, try_wait and wait.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <latch>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::latch l(2);
  CHECK(!l.try_wait());
  l.count_down();
  CHECK(!l.try_wait());
  l.count_down();
  CHECK(l.try_wait());
  l.wait();
  std::latch one(1);
  one.arrive_and_wait();
  CHECK(one.try_wait());
  CHECK(std::latch::max() > 0);
  printf("latch: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
