//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <compare>: three-way comparison categories and the comparison algorithms.
#include <compare>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK((1 <=> 2) < 0 && (2 <=> 2) == 0 && (3 <=> 2) > 0);
  CHECK((1.0 <=> 2.0) == std::partial_ordering::less);
  volatile double z = 0.0;
  double nan = z / z;
  CHECK((nan <=> 1.0) == std::partial_ordering::unordered);
  CHECK(std::is_lt(1 <=> 2) && std::is_gteq(2 <=> 2) && std::is_neq(1 <=> 2));
  CHECK(std::compare_three_way()(1, 2) < 0);
  CHECK(std::strong_order(1.0, 2.0) == std::strong_ordering::less);
  CHECK(std::weak_order(2.0, 2.0) == std::weak_ordering::equivalent);
  CHECK(std::partial_order(1, 2) == std::partial_ordering::less);
  static_assert(std::three_way_comparable<int>);
  static_assert(std::is_same_v<std::compare_three_way_result_t<double>, std::partial_ordering>);
  struct P { int a, b; auto operator<=>(const P &) const = default; };
  CHECK(P{1, 2} < P{1, 3} && P{1, 2} == P{1, 2});
  std::strong_ordering so = std::strong_ordering::greater;
  CHECK(so == std::strong_ordering::greater && std::weak_ordering(so) > 0);
  printf("compare: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
