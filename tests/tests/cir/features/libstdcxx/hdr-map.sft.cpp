//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <map>: map and multimap insertion, lookup, ordered iteration, erase and structured bindings.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <map>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::map<int, int> m;
  for (int i = 0; i < 50; ++i) m[(i * 7) % 50] = i;
  CHECK(m.size() == 50);
  int prev = -1;
  for (auto &[k, v] : m) { CHECK(k > prev); prev = k; (void)v; }
  CHECK(m.find(21)->second == 3);
  CHECK(m.count(49) == 1 && m.count(50) == 0);
  CHECK(m.lower_bound(10)->first == 10 && m.upper_bound(10)->first == 11);
  m.erase(21);
  CHECK(m.find(21) == m.end() && m.size() == 49);
  auto [it, ok] = m.emplace(21, 99);
  CHECK(ok && it->second == 99);
  auto [it2, ok2] = m.try_emplace(21, 100);
  CHECK(!ok2 && it2->second == 99);
  std::multimap<int, char> mm{{1, 'a'}, {1, 'b'}, {2, 'c'}};
  CHECK(mm.count(1) == 2 && mm.equal_range(1).first->second == 'a');
  std::map<int, int> n = m;
  CHECK(n == m);
  n.erase(n.begin(), n.find(25));
  CHECK(n.begin()->first == 25);
  printf("map: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
