//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <array>: array element access, bounds checking, tuple interface and constexpr use.
#include <array>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::array<int, 4> a{1, 2, 3, 4};
  CHECK(a.size() == 4 && a.at(2) == 3 && a.front() == 1 && a.back() == 4);
  int sum = 0;
  for (int v : a) sum += v;
  CHECK(sum == 10);
  CHECK(std::get<1>(a) == 2);
  auto [w, x, y, z] = a;
  CHECK(w + x + y + z == 10);
  bool caught = false;
  try { (void)a.at(9); } catch (...) { caught = true; }
  CHECK(caught);
  constexpr auto ca = std::to_array({3, 1, 2});
  static_assert(ca[0] == 3 && ca.size() == 3);
  std::array<int, 4> b = a;
  b.fill(7);
  CHECK(b != a && b[3] == 7);
  CHECK((a <=> b) == std::strong_ordering::less);
  a.swap(b);
  CHECK(a[0] == 7);
  CHECK(std::tuple_size_v<decltype(a)> == 4);
  printf("array: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
