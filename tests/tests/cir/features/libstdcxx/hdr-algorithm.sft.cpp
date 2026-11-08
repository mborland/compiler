//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <algorithm>: sort, search, partition, rotate, permutations and the ranges algorithms.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <algorithm>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  int a[8] = {5, 3, 8, 1, 9, 2, 7, 4};
  std::sort(a, a + 8);
  CHECK(std::is_sorted(a, a + 8));
  CHECK(a[0] == 1 && a[7] == 9);
  CHECK(std::binary_search(a, a + 8, 7));
  CHECK(!std::binary_search(a, a + 8, 6));
  CHECK(*std::lower_bound(a, a + 8, 6) == 7);
  std::reverse(a, a + 8);
  CHECK(a[0] == 9);
  CHECK(std::count_if(a, a + 8, [](int x) { return x % 2 == 0; }) == 3);
  CHECK(std::max_element(a, a + 8) == a);
  CHECK(*std::min_element(a, a + 8) == 1);
  int b[6] = {1, 1, 2, 2, 3, 3};
  CHECK(std::unique(b, b + 6) - b == 3);
  std::rotate(a, a + 1, a + 8);
  CHECK(a[7] == 9);
  int p[3] = {1, 2, 3};
  int n = 1;
  while (std::next_permutation(p, p + 3)) ++n;
  CHECK(n == 6);
  CHECK(std::clamp(15, 0, 10) == 10);
  CHECK(std::all_of(b, b + 6, [](int x) { return x > 0; }));
  int c[5] = {9, 4, 7, 1, 3};
  std::stable_sort(c, c + 5, [](int x, int y) { return x > y; });
  CHECK(c[0] == 9 && c[4] == 1);
  std::ranges::sort(c);
  CHECK(std::ranges::is_sorted(c));
  CHECK(std::ranges::find(c, 7) == c + 3);
  auto part = std::partition(c, c + 5, [](int x) { return x < 5; });
  CHECK(part - c == 3);
  printf("algorithm: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
