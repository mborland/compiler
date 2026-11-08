//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <initializer_list>: initializer_list parameters, deduction and iteration.
#include <initializer_list>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int sum(std::initializer_list<int> l) { int s = 0; for (int v : l) s += v; return s; }
struct S { int n, total; S(std::initializer_list<int> l) : n(int(l.size())), total(sum(l)) {} };
int main() {
  CHECK(sum({1, 2, 3, 4}) == 10);
  CHECK(sum({}) == 0);
  auto il = {5, 6, 7};
  CHECK(il.size() == 3 && *il.begin() == 5 && il.end() - il.begin() == 3);
  CHECK(sum(il) == 18);
  std::initializer_list<int> empty;
  CHECK(empty.size() == 0);
  S s{1, 2, 3};
  CHECK(s.n == 3 && s.total == 6);
  printf("initializer_list: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
