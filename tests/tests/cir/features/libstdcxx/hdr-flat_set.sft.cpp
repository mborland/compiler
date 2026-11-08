//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <flat_set>: flat_set and flat_multiset (C++23).
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <flat_set>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::flat_set<int> s;
  for (int i = 0; i < 30; ++i) s.insert((i * 7) % 30);
  CHECK(s.size() == 30 && *s.begin() == 0 && *(s.end() - 1) == 29);
  CHECK(s.contains(11) && !s.contains(30));
  s.erase(11);
  CHECK(!s.contains(11) && s.size() == 29);
  CHECK(*s.lower_bound(11) == 12);
  std::flat_multiset<int> ms{3, 1, 3};
  CHECK(ms.count(3) == 2 && ms.size() == 3);
  auto c = std::move(s).extract();
  CHECK(c.size() == 29);
  printf("flat_set: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
