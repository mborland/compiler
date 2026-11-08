//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <optional>: optional values, monadic operations, comparison and bad_optional_access.
#include <optional>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::optional<int> o;
  CHECK(!o && o.value_or(7) == 7);
  o = 5;
  CHECK(o.has_value() && *o == 5 && o.value() == 5);
#if __cplusplus > 202002L
  auto d = o.transform([](int x) { return x * 2; });
  CHECK(*d == 10);
  auto e = o.and_then([](int x) -> std::optional<int> { return x > 3 ? std::optional<int>(x) : std::nullopt; });
  CHECK(e == 5);
  CHECK(!std::optional<int>().or_else([] { return std::optional<int>(); }).has_value());
#endif
  o.reset();
  bool caught = false;
  try { (void)o.value(); } catch (const std::bad_optional_access &) { caught = true; }
  CHECK(caught);
  o.emplace(8);
  CHECK(o > std::optional<int>(3) && o != std::nullopt);
  struct P { int a, b; P(int x, int y) : a(x), b(y) {} };
  std::optional<P> p(std::in_place, 1, 2);
  CHECK(p->a == 1 && p->b == 2);
  CHECK(std::make_optional(3) == 3);
  printf("optional: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
