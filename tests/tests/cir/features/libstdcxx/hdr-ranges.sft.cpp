//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <ranges>: views (iota, filter, transform, take, reverse, zip, enumerate, join, chunk), fold_left and ranges::to.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <ranges>
#include <vector>
#include <functional>
#include <algorithm>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  namespace v = std::views;
  auto evens = v::iota(1, 11) | v::filter([](int x) { return x % 2 == 0; });
  int sum = 0;
  for (int x : evens) sum += x;
  CHECK(sum == 30);
  auto sq = v::iota(1) | v::transform([](int x) { return x * x; }) | v::take(5);
  int sqsum = 0;
  for (int x : sq) sqsum += x;
  CHECK(sqsum == 55);
#if __cplusplus > 202002L
  CHECK(std::ranges::fold_left(sq, 0, std::plus<>()) == 55);
#endif
  int a[3] = {1, 2, 3};
  auto rv = v::iota(0, 5) | v::reverse;
  CHECK(*rv.begin() == 4);
#if __cplusplus > 202002L
  std::vector<int> vec = v::iota(0, 5) | v::reverse | std::ranges::to<std::vector>();
  CHECK(vec.size() == 5 && vec[0] == 4 && vec[4] == 0);
  char c[3] = {'x', 'y', 'z'};
  int n = 0;
  for (auto [i, ch] : v::zip(a, c)) { n += i; CHECK(ch >= 'x'); }
  CHECK(n == 6);
  for (auto [idx, val] : v::enumerate(a)) CHECK(val == int(idx) + 1);
#endif
  std::vector<std::vector<int>> nested{{1, 2}, {3}, {4, 5, 6}};
  int tot = 0;
  for (int x : nested | v::join) tot += x;
  CHECK(tot == 21);
#if __cplusplus > 202002L
  auto ch = v::iota(0, 7) | v::chunk(3);
  CHECK(std::ranges::distance(ch) == 3);
#endif
  CHECK(std::ranges::count(v::iota(0, 100), 5) == 1);
  auto [mn, mx] = std::ranges::minmax(a);
  CHECK(mn == 1 && mx == 3);
  static_assert(std::ranges::range<std::vector<int>> && std::ranges::view<std::ranges::empty_view<int>>);
  printf("ranges: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
