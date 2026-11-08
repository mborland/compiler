//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <numeric>: accumulate, iota, gcd, lcm, partial sums, reductions and midpoint.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <numeric>
#include <functional>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  int a[6];
  std::iota(a, a + 6, 1);
  CHECK(a[0] == 1 && a[5] == 6);
  CHECK(std::accumulate(a, a + 6, 0) == 21);
  CHECK(std::accumulate(a, a + 6, 1, std::multiplies<int>()) == 720);
  CHECK(std::inner_product(a, a + 6, a, 0) == 91);
  int ps[6];
  std::partial_sum(a, a + 6, ps);
  CHECK(ps[5] == 21 && ps[2] == 6);
  int d[6];
  std::adjacent_difference(a, a + 6, d);
  CHECK(d[0] == 1 && d[5] == 1);
  CHECK(std::gcd(84, 36) == 12 && std::lcm(4, 6) == 12);
  CHECK(std::reduce(a, a + 6, 0) == 21);
  CHECK(std::transform_reduce(a, a + 6, a, 0) == 91);
  int ex[6];
  std::exclusive_scan(a, a + 6, ex, 0);
  CHECK(ex[0] == 0 && ex[5] == 15);
  CHECK(std::midpoint(10, 20) == 15 && std::midpoint(-3, 4) == 0);
  printf("numeric: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
