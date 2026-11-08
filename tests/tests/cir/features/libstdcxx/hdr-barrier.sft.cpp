//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <barrier>: barrier phases with a completion function.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <barrier>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  int phases = 0;
  std::barrier b(1, [&]() noexcept { ++phases; });
  b.arrive_and_wait();
  b.arrive_and_wait();
  CHECK(phases == 2);
  auto tok = b.arrive();
  b.wait(std::move(tok));
  CHECK(phases == 3);
  std::barrier c(2);
  c.arrive_and_drop();
  c.arrive_and_wait();
  printf("barrier: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
