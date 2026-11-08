//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <variant>: variant alternatives, visit, get_if, emplace and bad_variant_access.
#include <variant>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::variant<int, double, char> v = 5;
  CHECK(v.index() == 0 && std::get<int>(v) == 5);
  v = 2.5;
  CHECK(std::holds_alternative<double>(v) && std::get<1>(v) == 2.5);
  CHECK(std::get_if<int>(&v) == nullptr && *std::get_if<double>(&v) == 2.5);
  bool caught = false;
  try { (void)std::get<char>(v); } catch (const std::bad_variant_access &) { caught = true; }
  CHECK(caught);
  v.emplace<char>('x');
  int r = std::visit([](auto x) { return int(x); }, v);
  CHECK(r == 'x');
  struct D { int *c; explicit D(int *p) : c(p) {} ~D() { ++*c; } D(const D &o) : c(o.c) {} };
  int dtors = 0;
  {
    std::variant<std::monostate, D> w;
    w.emplace<D>(&dtors);
    CHECK(w.index() == 1);
  }
  CHECK(dtors >= 1);
  std::variant<int, long> a = 1, b = 2L;
  CHECK(a < b && a != b);
  auto vis = std::visit([](auto x, auto y) { return int(x) + int(y); }, a, b);
  CHECK(vis == 3);
  printf("variant: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
