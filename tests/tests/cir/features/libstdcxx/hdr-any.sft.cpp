//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <any>: any holding values, any_cast, reset and bad_any_cast.
#include <any>
#include <typeinfo>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::any a = 42;
  CHECK(a.has_value() && a.type() == typeid(int));
  CHECK(std::any_cast<int>(a) == 42);
  a = 3.5;
  CHECK(a.type() == typeid(double) && std::any_cast<double>(a) == 3.5);
  bool caught = false;
  try { (void)std::any_cast<int>(a); } catch (const std::bad_any_cast &) { caught = true; }
  CHECK(caught);
  struct Big { long v[8]; };
  Big b{};
  b.v[7] = 5;
  a = b;
  CHECK(std::any_cast<Big>(a).v[7] == 5);
  CHECK(std::any_cast<int>(&a) == nullptr);
  a.reset();
  CHECK(!a.has_value());
  a.emplace<int>(9);
  CHECK(std::any_cast<int>(a) == 9);
  auto m = std::make_any<long>(7L);
  CHECK(std::any_cast<long>(m) == 7);
  printf("any: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
