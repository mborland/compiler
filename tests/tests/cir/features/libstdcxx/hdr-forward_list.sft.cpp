//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <forward_list>: forward_list insertion, sort, merge, splice, reverse and remove.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <forward_list>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::forward_list<int> f{5, 3, 8, 1};
  f.push_front(7);
  f.sort();
  int prev = -1, n = 0;
  for (int v : f) { CHECK(v >= prev); prev = v; ++n; }
  CHECK(n == 5 && f.front() == 1);
  std::forward_list<int> g{2, 4, 6};
  g.merge(f);
  CHECK(std::distance(g.begin(), g.end()) == 8);
  g.remove(4);
  g.unique();
  g.reverse();
  CHECK(g.front() == 8);
  std::forward_list<int> h{100, 200};
  g.splice_after(g.before_begin(), h);
  CHECK(g.front() == 100 && h.empty());
  g.insert_after(g.begin(), 150);
  CHECK(*++g.begin() == 150);
  g.remove_if([](int v) { return v > 90; });
  CHECK(g.front() == 8);
  printf("forward_list: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
