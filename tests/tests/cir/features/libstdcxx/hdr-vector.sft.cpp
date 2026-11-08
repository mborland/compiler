//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <vector>: vector growth, insertion, erasure, comparison, vector<bool> and nesting.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <vector>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::vector<int> v;
  for (int i = 0; i < 1000; ++i) v.push_back(i);
  CHECK(v.size() == 1000 && v.capacity() >= 1000);
  long sum = 0;
  for (int x : v) sum += x;
  CHECK(sum == 499500);
  v.insert(v.begin() + 10, 3, -1);
  CHECK(v.size() == 1003 && v[10] == -1 && v[12] == -1 && v[13] == 10);
  v.erase(v.begin() + 10, v.begin() + 13);
  CHECK(v.size() == 1000 && v[10] == 10);
  v.resize(5);
  CHECK(v.size() == 5);
  std::vector<int> w{0, 1, 2, 3, 4};
  CHECK(v == w && (v <=> w) == std::strong_ordering::equal);
  std::vector<std::vector<int>> g(3, std::vector<int>(4, 2));
  CHECK(g[2][3] == 2 && g.size() == 3);
  std::vector<bool> b(100, false);
  b[37] = true; b[99] = true;
  CHECK(b[37] && b[99] && !b[38]);
  b.flip();
  CHECK(!b[37] && b[38]);
  v.emplace_back(42);
  CHECK(v.back() == 42);
  v.assign(3, 8);
  CHECK(v.size() == 3 && v[2] == 8);
  v.shrink_to_fit();
  v.clear();
  CHECK(v.empty());
  printf("vector: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
