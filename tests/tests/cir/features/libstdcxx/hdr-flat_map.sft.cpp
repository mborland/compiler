//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <flat_map>: flat_map and flat_multimap (C++23).
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <flat_map>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::flat_map<int, int> m;
  for (int i = 0; i < 20; ++i) m[(i * 7) % 20] = i;
  CHECK(m.size() == 20);
  CHECK(m.at(7) == 1 && m.find(100) == m.end());
  int prev = -1;
  for (auto [k, v] : m) { CHECK(k > prev); prev = k; (void)v; }
  m.erase(7);
  CHECK(m.count(7) == 0 && m.size() == 19);
  CHECK(m.lower_bound(10)->first == 10);
  std::flat_multimap<int, char> mm;
  mm.insert({1, 'a'}); mm.insert({1, 'b'}); mm.insert({0, 'z'});
  CHECK(mm.size() == 3 && mm.begin()->second == 'z' && mm.count(1) == 2);
  CHECK(m.keys().size() == 19 && m.values().size() == 19);
  printf("flat_map: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
