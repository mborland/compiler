//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <execution>: execution policies with the standard algorithms and numeric reductions.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <execution>
#include <vector>
#include <algorithm>
#include <numeric>
#include <functional>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::vector<int> v(1000);
  std::iota(v.begin(), v.end(), 1);
  std::for_each(std::execution::seq, v.begin(), v.end(), [](int &x) { x *= 2; });
  CHECK(v[999] == 2000);
  CHECK(std::reduce(std::execution::par, v.begin(), v.end(), 0L) == 1001000);
  std::sort(std::execution::par, v.begin(), v.end(), std::greater<int>());
  CHECK(v[0] == 2000 && v[999] == 2);
  CHECK(std::all_of(std::execution::par_unseq, v.begin(), v.end(), [](int x) { return x % 2 == 0; }));
  std::vector<int> w(1000);
  std::transform(std::execution::par, v.begin(), v.end(), w.begin(), [](int x) { return x / 2; });
  CHECK(w[0] == 1000);
  CHECK(std::count(std::execution::seq, w.begin(), w.end(), 5) == 1);
  static_assert(std::is_execution_policy_v<std::execution::sequenced_policy>);
  printf("execution: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
