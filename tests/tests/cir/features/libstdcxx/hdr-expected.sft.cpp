//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <expected>: expected values and errors, monadic operations and bad_expected_access (C++23).
#include <expected>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::expected<int, int> e = 5;
  CHECK(e.has_value() && *e == 5 && e.value() == 5);
  std::expected<int, int> u = std::unexpected(7);
  CHECK(!u && u.error() == 7 && u.value_or(1) == 1);
  auto t = e.transform([](int x) { return x * 2; });
  CHECK(*t == 10);
  auto a = u.or_else([](int err) -> std::expected<int, int> { return err + 1; });
  CHECK(*a == 8);
  auto c = e.and_then([](int x) -> std::expected<int, int> { return std::unexpected(x); });
  CHECK(!c && c.error() == 5);
  bool caught = false;
  try { (void)u.value(); } catch (const std::bad_expected_access<int> &x) { caught = x.error() == 7; }
  CHECK(caught);
  std::expected<void, int> v;
  CHECK(v.has_value());
  v = std::unexpected(2);
  CHECK(v.error() == 2);
  CHECK(e == std::expected<int, int>(5) && e != u);
  printf("expected: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
