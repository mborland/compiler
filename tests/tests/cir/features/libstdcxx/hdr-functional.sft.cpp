//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <functional>: function, bind, ref, mem_fn, not_fn, bind_front, invoke and the comparison functors.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <functional>
#include <memory>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::function<int(int, int)> f = [](int a, int b) { return a + b; };
  CHECK(f(2, 3) == 5);
  long big[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  std::function<long()> g = [big] { return big[7]; };
  CHECK(g() == 8);
  auto bd = std::bind(std::minus<int>(), std::placeholders::_1, 3);
  CHECK(bd(10) == 7);
  int v = 1;
  auto inc = std::bind([](int &x) { x += 5; }, std::ref(v));
  inc();
  CHECK(v == 6);
  struct S { int m; int get() const { return m; } } s{4};
  CHECK(std::mem_fn(&S::get)(s) == 4 && std::mem_fn(&S::m)(s) == 4);
  CHECK(std::not_fn([](int x) { return x > 0; })(-1));
  CHECK(std::bind_front([](int a, int b) { return a * b; }, 6)(7) == 42);
  CHECK(std::invoke(&S::get, s) == 4);
  CHECK(std::plus<>()(1, 2.5) == 3.5 && std::less<>()(1, 2) && std::greater<int>()(2, 1));
  CHECK(std::hash<int>()(7) == std::hash<int>()(7));
  std::function<int()> empty;
  CHECK(!empty && !!f);
#if __cplusplus > 202002L
  std::move_only_function<int()> mof = [p = std::make_unique<int>(3)] { return *p; };
  CHECK(mof() == 3);
#endif
  printf("functional: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
