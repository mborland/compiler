//type:rp
//options:--c++20 --diag_suppress=cir_warning:--c++20 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <utility>: pair, move, forward, swap, exchange, integer_sequence, cmp_* and to_underlying.
#include <utility>
#include <tuple>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::pair<int, double> p(1, 2.5);
  auto q = std::make_pair(1, 2.5);
  CHECK(p == q && p.first == 1 && p.second == 2.5);
  int a = 1, b = 2;
  std::swap(a, b);
  CHECK(a == 2 && b == 1);
  CHECK(std::exchange(a, 9) == 2 && a == 9);
  CHECK(std::cmp_less(-1, 1u) && !std::cmp_greater(-1, 1u));
  CHECK(std::in_range<unsigned char>(255) && !std::in_range<unsigned char>(256));
  enum class E : short { A = 3 };
#if __cplusplus > 202002L
  CHECK(std::to_underlying(E::A) == 3);
#endif
  auto seq = std::make_index_sequence<4>{};
  CHECK(seq.size() == 4);
  int x = 5;
  int y = std::move(x);
  CHECK(y == 5);
  const int &cr = std::as_const(x);
  CHECK(cr == 5);
  std::pair<int, int> pp(std::piecewise_construct, std::forward_as_tuple(7), std::forward_as_tuple(8));
  CHECK(pp.first == 7 && pp.second == 8);
  CHECK(std::pair<int, int>(1, 2) < std::pair<int, int>(1, 3));
  printf("utility: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
