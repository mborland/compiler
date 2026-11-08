//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <concepts>: concept checks and constrained templates.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <concepts>
#include <functional>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
struct B2 {};
struct D2 : B2 {};
template <std::integral T> T twice(T x) { return x * 2; }
template <std::floating_point T> T twice(T x) { return x * 2; }
template <typename T> requires std::integral<T> int kind(T) { return 1; }
template <typename T> requires std::floating_point<T> int kind(T) { return 2; }
int main() {
  static_assert(std::integral<int> && !std::integral<double>);
  static_assert(std::floating_point<float> && std::signed_integral<long>);
  static_assert(std::same_as<int, int> && std::derived_from<D2, B2>);
  static_assert(std::convertible_to<int, double> && std::invocable<int (*)(int), int>);
  static_assert(std::regular<int> && std::totally_ordered<double> && std::copyable<int>);
  static_assert(std::default_initializable<int> && std::movable<int> && std::swappable<int>);
  static_assert(std::equality_comparable_with<int, long> && !std::integral<B2>);
  static_assert(std::predicate<bool (*)(int), int> && std::relation<std::equal_to<>, int, int>);
  CHECK(twice(3) == 6 && twice(1.5) == 3.0);
  CHECK(kind(1) == 1 && kind(1.0) == 2 && kind('c') == 1);
  printf("concepts: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
