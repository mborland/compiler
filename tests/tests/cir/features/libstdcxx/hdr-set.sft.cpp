//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <set>: set and multiset insertion, lookup, ordered iteration and erase.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <set>
#include <functional>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::set<int> s;
  for (int i = 0; i < 100; ++i) s.insert((i * 37) % 100);
  CHECK(s.size() == 100);
  CHECK(*s.begin() == 0 && *s.rbegin() == 99);
  CHECK(s.count(50) == 1 && !s.insert(50).second);
  CHECK(*s.lower_bound(60) == 60 && *s.upper_bound(60) == 61);
  s.erase(50);
  CHECK(s.count(50) == 0 && s.size() == 99);
  int prev = -1;
  for (int v : s) { CHECK(v > prev); prev = v; }
  std::multiset<int> ms{3, 1, 3, 2, 3};
  CHECK(ms.count(3) == 3 && ms.size() == 5);
  ms.erase(3);
  CHECK(ms.size() == 2);
  std::set<int, std::greater<int>> g{1, 5, 3};
  CHECK(*g.begin() == 5);
  printf("set: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
