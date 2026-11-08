//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <list>: list insertion, splice, sort, merge, unique, remove and iteration.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <list>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::list<int> l{4, 2, 9, 2, 7};
  l.push_front(1);
  l.push_back(10);
  CHECK(l.size() == 7 && l.front() == 1 && l.back() == 10);
  l.sort();
  l.unique();
  CHECK(l.size() == 6);
  l.remove(9);
  CHECK(l.size() == 5);
  std::list<int> m{3, 5};
  l.merge(m);
  CHECK(m.empty() && l.size() == 7);
  int prev = 0;
  for (int v : l) { CHECK(v >= prev); prev = v; }
  std::list<int> s{100, 101};
  l.splice(l.begin(), s);
  CHECK(l.front() == 100 && s.empty());
  l.reverse();
  CHECK(l.back() == 100);
  auto it = l.begin();
  std::advance(it, 2);
  l.erase(it);
  CHECK(l.size() == 8);
  l.assign(3, 7);
  CHECK(l.size() == 3 && l.back() == 7);
  printf("list: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
