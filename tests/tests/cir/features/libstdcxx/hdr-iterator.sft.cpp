//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <iterator>: iterator operations, adaptors, inserters and the range access functions.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <iterator>
#include <vector>
#include <algorithm>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  int a[5] = {1, 2, 3, 4, 5};
  CHECK(std::distance(a, a + 5) == 5);
  int *p = a;
  std::advance(p, 3);
  CHECK(*p == 4 && *std::next(a, 2) == 3 && *std::prev(a + 5) == 5);
  std::reverse_iterator<int *> ri(a + 5);
  CHECK(*ri == 5 && *(ri + 2) == 3);
  std::vector<int> v;
  std::copy(a, a + 5, std::back_inserter(v));
  CHECK(v.size() == 5 && v[4] == 5);
  std::vector<int> w;
  std::copy(std::make_move_iterator(a), std::make_move_iterator(a + 5), std::inserter(w, w.begin()));
  CHECK(w.size() == 5);
  CHECK(std::size(a) == 5 && std::data(v) == &v[0] && !std::empty(v));
  CHECK(std::ranges::begin(a) == a && std::ranges::end(a) == a + 5);
  std::counted_iterator<int *> ci(a, 3);
  CHECK(ci.count() == 3);
  CHECK((std::default_sentinel_t{} == std::counted_iterator<int *>(a, 0)));
  static_assert(std::random_access_iterator<int *> && std::forward_iterator<std::vector<int>::iterator>);
  static_assert(std::is_same_v<std::iter_value_t<int *>, int>);
  CHECK(*std::rbegin(a) == 5 && std::ssize(a) == 5);
  printf("iterator: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
