//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <tuple>: tuple construction, get, tie, apply, tuple_cat, comparison and structured bindings.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <tuple>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  auto t = std::make_tuple(1, 2.5, 'c');
  CHECK(std::get<0>(t) == 1 && std::get<1>(t) == 2.5 && std::get<2>(t) == 'c');
  int a; double b;
  std::tie(a, b, std::ignore) = t;
  CHECK(a == 1 && b == 2.5);
  auto [x, y, z] = t;
  CHECK(x == 1 && y == 2.5 && z == 'c');
  CHECK(std::apply([](int p, double q, char r) { return p + int(q) + r; }, t) == 1 + 2 + 'c');
  auto c = std::tuple_cat(std::make_tuple(1), std::make_tuple(2, 3));
  CHECK(std::tuple_size_v<decltype(c)> == 3 && std::get<2>(c) == 3);
  CHECK(std::make_tuple(1, 2) < std::make_tuple(1, 3));
  CHECK(std::make_tuple(1, 2) == std::make_tuple(1, 2));
  std::tuple<int &, long> r(a, 4L);
  std::get<0>(r) = 99;
  CHECK(a == 99);
  std::tuple<> e;
  CHECK(std::tuple_size_v<decltype(e)> == 0);
  std::tuple<int, int> sw1(1, 2), sw2(3, 4);
  sw1.swap(sw2);
  CHECK(std::get<0>(sw1) == 3);
  printf("tuple: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
